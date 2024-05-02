cmake_minimum_required(VERSION 3.13)
project(xboxaudio)

# Set the C standard to use
set(CMAKE_C_STANDARD 99)

# Find the USB library
find_package(PkgConfig REQUIRED)
pkg_check_modules(USB REQUIRED libusb-1.0)

# Set the include directories
include_directories(${USB_INCLUDE_DIRS})

# Set the source file
set(SOURCE_FILE xboxaudio.c)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILE})

# Link the USB library
target_link_libraries(${PROJECT_NAME} ${USB_LIBRARIES} m)
