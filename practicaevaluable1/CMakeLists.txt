cmake_minimum_required(VERSION 3.20)
project(practicaevaluable1 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

add_executable(broker broker.c)
add_executable(gestor_claves gestor_claves.c)
add_executable(cliente cliente.c)