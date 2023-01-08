#include <stdio.h>
#include <thread>
#include <sstream>
#include <fstream>
#include <string>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "GameVersion.h"
#include "platforms.h"

using namespace std;

static bool glfwInitialized = 0;
static string resourcesFolder;

// #define ASSERT(x) if (!(x)) __debugbreak(); //TODO: Expand this to other compilers beyond just MSVC!
//TODO: Maybe use https://github.com/GPMueller/mwe-cpp-exception

#define GLCall(x) customGLClearErrors();\
    x;\
    customGLCheckForErrors(#x, __FILE__, __LINE__)

void customGLClearErrors() {
    while (glGetError() != GL_NO_ERROR);
}

bool customGLCheckForErrors(const char* functionName, const char* fileName, int lineNumber) {
    GLenum error;
    bool success = true;
    while ((error = glGetError()) != GL_NO_ERROR) {
        // printf("[OpenGL Error] (%d)\nCalled from %s in %s:%s", error, functionName, fileName, lineNumber);
        printf("[OpenGL Error] (%d): %s\nin %s:line %d\n\n", error, functionName, fileName, lineNumber);
        success = false;
    }
    return success;
}

void errorCallback(int errorCode, const char* description) {
    stringstream ss;
    ss << "GLFW Error code: " << errorCode << "\n";
    ss << description;
    ss << "\n";

    const char* message = ss.str().c_str();
    fprintf(stderr, "%s", message);
}

int tryCreateWindow(const char* title, int width, int height, GLFWwindow*& window) {
    if (!glfwInitialized) {
        if (!glfwInit()) {
            fprintf(stderr, "GLFW initialization failed!\n");
            return 1;
        }

        //NOTE: Let's require a certain (old) version of OpenGL or newer...
        //Like OpenGL 3.0+. HOWEVER,
        //NOTE: Context profiles are only available in OpenGL 3.2+, so we'll require that!
        //TODO: Try to require 4.6, then if we get the error (during callback) of "Requested OpenGL version 4.6, got version 4.1", then request that version instead!
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

        //NOTE: BEFORE doing this, I was getting the following results:
        //  Windows:        OpenGL 4.6          ==> NOW OpenGL 3.2
        //  MacOS:          OpenGL 2.1          ==> NOW OpenGL 4.1!
        //  Linux:          OpenGL 4.6          ==> NOW OpenGL 3.2
        //So, we set it to use OpenGL Core profile with forward compatibility: 
        glfwWindowHint(GLFW_OPENGL_PROFILE,             GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,      GL_TRUE);

        glfwSetErrorCallback(errorCallback);
    }

    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (window == NULL) {
        fprintf(stderr, "Failed to create window or OpenGL context!\n");
        glfwTerminate();
        return 2;
    }

    if (!glfwInitialized) {
        glfwMakeContextCurrent(window);
        int version = gladLoadGL(glfwGetProcAddress);
        if (version == 0) {
            printf("Failed to initialize OpenGL context with GLAD!\n");
            return 3;
        }
        printf("Loaded OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
    }
    glfwInitialized = true;
    return 0;
}

struct ShaderProgramSource {
    string vertexSource;
    string fragmentSource;
};

ShaderProgramSource parseShader(const string& filePath) {
    enum class ShaderType {
        NONE = -1,
        VERTEX = 0,
        FRAGMENT = 1
    };

    string resourcesFilePath = filePath;
    if (resourcesFilePath.find(resourcesFolder) == string::npos)
        resourcesFilePath = resourcesFolder + "/" + resourcesFilePath;
    printf("Parsing shader from %s\n", resourcesFilePath.c_str());

    //NOTE: Already opens the file:
    ifstream stream(resourcesFilePath);

    string line;
    stringstream ss[2];
    ShaderType type = ShaderType::NONE;
    while (getline(stream, line)) {
        if (line.find("#shader") != string::npos) {
            if (line.find("vertex") != string::npos)
                type = ShaderType::VERTEX;
            else if (line.find("fragment") != string::npos)
                type = ShaderType::FRAGMENT;
        } else {
            ss[(int) type] << line << "\n";
        }
    }

    return {
        ss[(int) ShaderType::VERTEX].str(),
        ss[(int) ShaderType::FRAGMENT].str()
    };
}

unsigned int compileShader(unsigned int type, const string& source) {
    GLCall(unsigned int id = glCreateShader(type));
    const char* src = source.c_str();
    GLCall(glShaderSource(id, 1, &src, NULL));
    GLCall(glCompileShader(id));

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (!result) {
        int length;
        GLCall(glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length));
        char* message = new char[length]; //(char*) alloca(length * sizeof(char)); //NOTE: Allocated dynamically on the stack!
        GLCall(glGetShaderInfoLog(id, length, &length, message));
        fprintf(stderr, "%s\n%s\n", "Failed to compile a shader!", message);

        GLCall(glDeleteShader(id));
        return 0;
    }
    return id;
}

unsigned int createShader(const string& vertexShader, const string& fragmentShader) {
    unsigned int programId = glCreateProgram();
    unsigned int vs = compileShader(GL_VERTEX_SHADER, vertexShader);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fragmentShader);
    
    GLCall(glAttachShader(programId, vs));
    GLCall(glAttachShader(programId, fs));
    GLCall(glLinkProgram(programId));
    GLCall(glValidateProgram(programId));

    GLCall(glDetachShader(programId, vs));
    GLCall(glDetachShader(programId, fs));

    GLCall(glDeleteShader(vs));
    GLCall(glDeleteShader(fs));
    return programId;
}

//NOTE: Huge thanks to https://stackoverflow.com/questions/2896600/how-to-replace-all-occurrences-of-a-character-in-string
void replaceAll(string& source, const string& from, const string& to) {
    string newString;
    newString.reserve(source.length());  // avoids a few memory allocations

    size_t lastPos = 0;
    size_t findPos;

    while((findPos = source.find(from, lastPos)) != string::npos) {
        newString.append(source, lastPos, findPos - lastPos);
        newString += to;
        lastPos = findPos + from.length();
    }

    // Care for the rest after last occurrence
    newString.append(source, lastPos, source.length() - lastPos);
    source.swap(newString);
}

int main(int argCount, char* args[]) {    
    printf(PROJECT_NAME " v" PROJECT_VERSION "\n");

    GLFWwindow* window;
    int initError = tryCreateWindow("Pixel Art Game", 800, 600, window);
    if (initError != 0) {
        fprintf(stderr, "%s%d\n", "Exiting with initialization exit code ", initError);
        return initError;
    }

    printf("%s%s\n", "Platform: ", getPlatformName());
    printf("\n");

    const int VERTEX_COUNT = 4;
    const int POSITION_COUNT = 2 * VERTEX_COUNT;
    const int INDEX_COUNT = 6;
    float r = 0.5f;

    //Triangle layout:
    //  1-----3
    //  | \   |
    //  |   \ |
    //  0-----2
    
    float positions[POSITION_COUNT] = {
        -r, -r,
        -r,  r,
         r, -r,
         r,  r,
    };

    unsigned int indices[INDEX_COUNT] = {
        0, 1, 2,
        3, 2, 1
    };

    unsigned int vertexArrayId;
    GLCall(glGenVertexArrays(1, &vertexArrayId));
    GLCall(glBindVertexArray(vertexArrayId));

    unsigned int vertexBufferId;
    GLCall(glGenBuffers(1, &vertexBufferId));
    GLCall(glBindBuffer(GL_ARRAY_BUFFER, vertexBufferId));
    GLCall(glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW));

    GLCall(glEnableVertexAttribArray(0));
    GLCall(glVertexAttribPointer(0, 2, GL_FLOAT, false, 2 * sizeof(float), 0));

    unsigned int indexBufferId;
    GLCall(glGenBuffers(1, &indexBufferId));
    GLCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferId));
    GLCall(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW));

    string exeFilePath = args[0];
    replaceAll(exeFilePath, "\\", "/");
    size_t indexOfLastFolderSeparator = exeFilePath.find_last_of("/");
    
    resourcesFolder = exeFilePath.substr(0, indexOfLastFolderSeparator);
    printf("Resources folder path = %s\n", resourcesFolder.c_str());

    ShaderProgramSource source = parseShader("resources/shaders/Basic.glsl");

    printf("\n\n");
    printf("VERTEX PROGRAM LOADED:\n%s\n", source.vertexSource.c_str());
    printf("FRAGMENT PROGRAM LOADED:\n%s\n", source.fragmentSource.c_str());

    unsigned int shaderId = createShader(source.vertexSource, source.fragmentSource);
    GLCall(glUseProgram(shaderId));

    while (!glfwWindowShouldClose(window)) {
        //NOTE: Now that we have modern OpenGL loaded from glad (the library),
        //We can use GL calls!
        GLCall(glClear(GL_COLOR_BUFFER_BIT));

        GLCall(glDrawElements(GL_TRIANGLES, INDEX_COUNT, GL_UNSIGNED_INT, NULL));

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    printf("Done!\n");

    GLCall(glDeleteProgram(shaderId));
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
