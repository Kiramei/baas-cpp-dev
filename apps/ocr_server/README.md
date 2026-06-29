### Workflow
1. Build binaries
    - platforms : Windows, Linux, MacOS
    - BuildType : Release
    - Target    : BAAS_ocr_server

2. Test Code
    - Use python unittest to test all the api and check response
    - OCR test images are not stored in this repository. The tests fetch
      `ocr_test_images` from `resources.lock.json` automatically when
      `apps/ocr_server/test/test_images` is missing.
    - working directory : folder `BAAS_Cpp`
    - python version : 3.9.18
    - command : 
        1. install dependencies
            ```shell
               pip install -r apps/ocr_server/test/requirements.txt
            ```
        2. run test
           ```shell
              python -m unittest discover -s apps/ocr_server/test -p "*.py"
           ```
    If test passed:
3. Push binaries to corresponding repository branch
    - [windows-x64](https://github.com/pur1fying/BAAS_Cpp_prebuild/tree/windows-x64)             
    - [linux-x64](https://github.com/pur1fying/BAAS_Cpp_prebuild/tree/linux-x64)                 
    - [macos-arm64](https://github.com/pur1fying/BAAS_Cpp_prebuild/tree/macos-arm64)             
    - [android-arm64-v8a](https://github.com/pur1fying/BAAS_Cpp_prebuild/tree/android-arm64-v8a) 
    - [android-x86_64](https://github.com/pur1fying/BAAS_Cpp_prebuild/tree/android-x86_64)     

   **note**: Only update_reference changed files so that the ocr_server updater will not copy all the files every time.


### Known Issues
1. MacOS & Linux : python will destroy the shared_memory automatically when code exit
2. api "get_text_boxes" is not implemented
3. update_reference test code 
    - [x] init / release model  
    - [x] start / stop server 
    - [x] ocr 
    - [x] ocr_for_single_line
    - [x] enable / disable thread pool
    - [x] create / release shared memory
    - [x] get_text_boxes
    - [x] multithread request test

### Android Support
1. [OpenCV 4.9.0 android sdk](https://release-assets.githubusercontent.com/github-production-release-asset/5108051/eb6f2dc7-a522-4eec-92e5-264bf23fc9c1?sp=r&sv=2018-11-09&sr=b&spr=https&se=2025-10-18T13%3A44%3A50Z&rscd=attachment%3B+filename%3Dopencv-4.9.0-android-sdk.zip&rsct=application%2Foctet-stream&skoid=96c2d410-5711-43a1-aedd-ab1947aa7ab0&sktid=398a6654-997b-47e9-b12b-9515b896b4de&skt=2025-10-18T12%3A44%3A38Z&ske=2025-10-18T13%3A44%3A50Z&sks=b&skv=2018-11-09&sig=v5fUuxYONOGNRiGhMKbzDbsyTN9gqwvujqprKHdcGSM%3D&jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmVsZWFzZS1hc3NldHMuZ2l0aHVidXNlcmNvbnRlbnQuY29tIiwia2V5Ijoia2V5MSIsImV4cCI6MTc2MDc5NTIxOSwibmJmIjoxNzYwNzkxNjE5LCJwYXRoIjoicmVsZWFzZWFzc2V0cHJvZHVjdGlvbi5ibG9iLmNvcmUud2luZG93cy5uZXQifQ.PfGRGTEr_SkittPDs5rRb5VMfVU_WUwUH498akpzV_I&response-content-disposition=attachment%3B%20filename%3Dopencv-4.9.0-android-sdk.zip&response-content-type=application%2Foctet-stream) 
2. [ONNXRuntime android lib build guide](https://onnxruntime.ai/docs/build/android.html#build-onnx-runtime-for-android)
3. ONNXRuntime Build Tips : 
   - Onnxruntime version : 1.22.1 
   - NDK version >= 26
   - Architectures : arm64-v8a, x86_64, armeabi-v7a, x86

4. build command
Use the Conan Android profile for the target ABI. The NDK path is supplied
through Conan conf and must point at your local NDK.
```shell
python deploy/conan/scripts/manage_recipes.py export

conan install deploy/conan ^
      -of build/conan/android-clang-release-ocr-arm64-v8a ^
      -pr:h=deploy/conan/profiles/android-clang-arm64-v8a-release ^
      -pr:h=deploy/conan/profiles/dependency-versions-default ^
      -c:h tools.android:ndk_path="D:\AndroidSDK\ndk\29.0.14206865" ^
      -o onnxruntime_use_cuda=False ^
      -o use_ffmpeg=False ^
      -o use_benchmark=False ^
      --build=missing ^
      --no-remote

cmake --preset conan-android-clang-release-ocr-arm64-v8a
```

```shell
cmake --build --preset conan-android-clang-release-ocr-arm64-v8a
```
