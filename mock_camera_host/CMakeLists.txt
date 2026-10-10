cmake_minimum_required(VERSION 3.16)
project(MockCameraHost LANGUAGES CXX)

# VMS_v2 앱 코드와 완전히 분리된 독립 빌드.
# 목업 카메라 호스트는 서버/카메라 대역을 흉내내는 별도 프로세스라
# 메인 CMakeLists.txt와 섞지 않는다 (docs/dev_execution_plan.md Phase 0 참고).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(event_broadcaster event_broadcaster.cpp)
target_link_libraries(event_broadcaster PRIVATE Qt6::Core Qt6::Network)

# cgi_mock(Phase 0의 HTTP CGI 방식 zoom/focus mock)은 Phase 2에서 ONVIF PTZ/Imaging SOAP으로
# 전환되며 완전히 대체됐다 — 메인 앱 어디서도 참조하지 않는 걸 확인하고 Phase 6에서 삭제.

add_executable(onvif_mock onvif_mock.cpp)
target_link_libraries(onvif_mock PRIVATE Qt6::Core Qt6::Network)
