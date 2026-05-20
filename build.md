<!-- python tools/ave.py build android sample/TriangleGame --sdk-dir D:\Setup\android_sdk -->
python tools/ave.py build android sample/TriangleGame --sdk-dir D:\Setup\android_sdk --vulkan-sdk D:\Setup\VulkanSDK

adb install -r sample\TriangleGame\build\android\app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n com.example.trianglegame/com.ave.engine.AveActivity

adb shell am force-stop com.example.trianglegame

taskkill /F /IM emulator.exe /T 
adb logcat *:E