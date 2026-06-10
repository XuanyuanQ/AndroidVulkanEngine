<!-- python tools/ave.py build android sample/TriangleGame --sdk-dir D:\Setup\android_sdk -->
python tools/ave.py build android sample/TriangleGame --sdk-dir D:\Setup\android_sdk --vulkan-sdk D:\Setup\VulkanSDK --no-gradle

adb install -r sample\TriangleGame\build\android\app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n com.example.trianglegame/com.example.trianglegame.GameActivity

adb shell am force-stop com.example.trianglegame

taskkill /F /IM emulator.exe /T 
adb logcat *:E

adb shell setprop debug.ave.dynamic_rendering 1
adb shell setprop debug.ave.openxr 1


distributionUrl=https\://services.gradle.org/distributions/gradle-8.7-bin.zip
