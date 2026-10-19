# Self-contained pinned face detector, including pretrained model weights.
function(ascii_chat_add_face_detector target)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    set(sources
        ${root}/deps/libfacedetection/facedetectcnn.cpp
        ${root}/deps/libfacedetection/facedetectcnn-model.cpp
        ${root}/deps/libfacedetection/facedetectcnn-data.cpp)
    target_sources(${target} PRIVATE ${sources} ${root}/lib/video/face_detect.cpp)
    target_include_directories(${target} PRIVATE ${root}/deps/libfacedetection ${root}/include)
    # Retain ASan and its C++ runtime ABI, without integer instrumentation in upstream inference loops.
    set_source_files_properties(${sources} PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON SKIP_UNITY_BUILD_INCLUSION ON
        COMPILE_OPTIONS "-w;-O3;-fno-sanitize=undefined,integer,nullability,implicit-conversion,float-divide-by-zero")
    set_source_files_properties(${root}/lib/video/face_detect.cpp PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON SKIP_UNITY_BUILD_INCLUSION ON)
endfunction()
