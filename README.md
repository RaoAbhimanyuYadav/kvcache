# C++ Sanity Suite

A robust C++ project template using **Modern CMake** and **GoogleTest (GTest)**. This project includes fully automated dependency management via `FetchContent` and continuous integration through GitHub Actions.

---

## 🚀 Getting Started

Follow these steps to clone, configure, build, and run the project locally on a completely fresh machine.

### Prerequisites

Ensure you have the following installed on your system:

- **CMake** (v3.20 or higher)
- **C++ Compiler** (Clang 17+)
- **Git**

> 💡 **Note:** You do **not** need to install GoogleTest manually. CMake will download and configure it automatically during the setup phase.

### 1. Clone the Repository

Open your terminal or command prompt and run:

```bash
git clone https://github.com/RaoAbhimanyuYadav/kvcache
cd kvcache
```

### 2. Configure the Project

Create the build directory structure and download dependencies:

```bash
cmake -S . -B build
```

### 3. Build the Target

Compile the source code and the test suites:

```bash
cmake --build build
```

### 4. Run the Tests

Execute the GoogleTest suite using CTest (CMake's built-in test runner):

```bash
ctest --test-dir build --output-on-failure
```

---

## 🛠️ Project Architecture

- **`CMakeLists.txt`**: Core build script. Uses `FetchContent` to safely fetch GoogleTest from source.
- **`test_main.cpp`**: Test runner components. Uses `gtest_discover_tests()` for automated, dynamic post-build test registration.
- **`.github/workflows/ci.yml`**: Continuous Integration engine.

---

## 🤖 Continuous Integration (GitHub Actions)

This project has **GitHub Actions** built right in. You don't need to configure anything locally to make it work.

### How it Works:

1. Every time you `git push` code to the `main` branch or open a **Pull Request**, GitHub boots up a clean, isolated cloud virtual machine (`ubuntu-latest`).
2. The pipeline handles the setup automatically: **Checkout ➡️ CMake Configure ➡️ Parallel Build ➡️ CTest Execution**.
3. You can view live tracking and detailed console logs by clicking on the **Actions** tab at the top of this GitHub repository page.
