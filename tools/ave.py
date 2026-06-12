#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ENGINE_ROOT = Path(__file__).resolve().parents[1]
ANDROID_TEMPLATE = ENGINE_ROOT / "templates" / "android"


def read_project(project_dir: Path) -> dict:
    project_xml = project_dir / "project.xml"
    if not project_xml.exists():
        raise RuntimeError(f"Missing {project_xml}")

    root = ET.parse(project_xml).getroot()
    if root.tag != "Project":
        raise RuntimeError("project.xml root must be <Project>")

    return {
        "name": root.attrib.get("name", "AveGame"),
        "package": root.attrib.get("package", "com.ave.game"),
        "entry_scene": root.attrib.get("entryScene", "scenes/main.scene.xml"),
        "orientation": root.attrib.get("orientation", "landscape"),
        "android_activity": root.attrib.get("androidActivity", "com.ave.engine.AveActivity"),
    }


def validate_scene(project_dir: Path, entry_scene: str) -> None:
    scene_path = project_dir / entry_scene
    if not scene_path.exists():
        raise RuntimeError(f"Missing entry scene: {scene_path}")

    root = ET.parse(scene_path).getroot()
    if root.tag != "Scene":
        raise RuntimeError("entry scene root must be <Scene>")

    objects = root.findall(".//GameObject")
    object_count = len(objects)
    if object_count == 0:
        raise RuntimeError("scene must contain at least one <GameObject>")

    seen_ids: dict[str, str] = {}
    seen_names: dict[str, str] = {}
    for obj in objects:
        object_id = obj.attrib.get("id", "")
        object_name = obj.attrib.get("name", object_id)
        if not object_id:
            raise RuntimeError("GameObject must define a non-empty id attribute")

        previous_name = seen_ids.get(object_id)
        if previous_name is not None:
            raise RuntimeError(
                f"Duplicate GameObject id '{object_id}' in {entry_scene} "
                f"(used by '{previous_name}' and '{object_name}')"
            )
        seen_ids[object_id] = object_name

        previous_id = seen_names.get(object_name)
        if previous_id is not None:
            raise RuntimeError(
                f"Duplicate GameObject name '{object_name}' in {entry_scene} "
                f"(used by id '{previous_id}' and id '{object_id}')"
            )
        seen_names[object_name] = object_id


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def replace_text(path: Path, replacements: dict[str, str]) -> None:
    text = path.read_text(encoding="utf-8")
    for key, value in replacements.items():
        text = text.replace(key, value)
    path.write_text(text, encoding="utf-8")


def package_project_data(project_dir: Path, output_dir: Path) -> None:
    assets_dir = output_dir / "app" / "src" / "main" / "assets"
    if assets_dir.exists():
        shutil.rmtree(assets_dir)
    assets_dir.mkdir(parents=True)

    for name in ["project.xml", "scenes", "prefabs", "materials", "assets", "shaders", "meshes", "textures"]:
        src = project_dir / name
        if src.is_dir():
            shutil.copytree(src, assets_dir / name)
        elif src.is_file():
            shutil.copy2(src, assets_dir / name)


def package_scripts(project_dir: Path, output_dir: Path, package_name: str) -> None:
    scripts_dir = project_dir / "scripts"
    if not scripts_dir.exists():
        return

    java_root = output_dir / "app" / "src" / "main" / "java"
    package_dir = java_root / Path(package_name.replace(".", "/"))
    package_dir.mkdir(parents=True, exist_ok=True)

    for script in scripts_dir.glob("*.java"):
        shutil.copy2(script, package_dir / script.name)


def write_local_properties(output_dir: Path, sdk_dir: str | None) -> None:
    candidates = [
        sdk_dir,
        os.environ.get("ANDROID_HOME"),
        os.environ.get("ANDROID_SDK_ROOT"),
        r"D:\Setup\android_sdk",
        str(Path.home() / "AppData" / "Local" / "Android" / "Sdk"),
        str(Path.home() / "Library" / "Android" / "sdk"),
        str(Path.home() / "Android" / "Sdk"),
    ]

    selected = next((Path(path) for path in candidates if path and Path(path).exists()), None)
    if selected is None:
        print("[warn] Android SDK not found. Set ANDROID_HOME or pass --sdk-dir.")
        return

    escaped = str(selected).replace("\\", "\\\\").replace(":", "\\:")
    (output_dir / "local.properties").write_text(f"sdk.dir={escaped}\n", encoding="utf-8")


def compile_shaders(project_dir: Path, output_dir: Path) -> None:
    shader_dir = project_dir / "shaders"
    if not shader_dir.exists():
        return

    glslc = shutil.which("glslc")
    spv_dir = output_dir / "app" / "src" / "main" / "assets" / "compiled_shaders"
    compile_shaders_to_dir(project_dir, spv_dir, glslc)


def compile_shaders_to_dir(project_dir: Path, spv_dir: Path, glslc: str | None = None) -> None:
    shader_dir = project_dir / "shaders"
    if not shader_dir.exists():
        return

    if glslc is None:
        glslc = shutil.which("glslc")

    spv_dir.mkdir(parents=True, exist_ok=True)

    for shader in shader_dir.iterdir():
        if shader.suffix not in [".vert", ".frag", ".comp"]:
            continue

        out = spv_dir / f"{shader.name}.spv"
        if glslc is None:
            print(f"[warn] glslc not found, shader left as source: {shader.name}")
            continue

        subprocess.run([glslc, str(shader), "-o", str(out)], check=True)


def run_gradle(output_dir: Path) -> None:
    gradlew = output_dir / ("gradlew.bat" if os.name == "nt" else "gradlew")
    print("gradlew:",gradlew)
    if gradlew.exists():
        command = [str(gradlew), "assembleDebug"]
    else:
        gradle = shutil.which("gradle")
        if gradle is None:
            gradle_candidates = sorted(Path.home().glob(".gradle/wrapper/dists/gradle-*/**/bin/gradle"))
            stable_candidates = [
                candidate for candidate in gradle_candidates
                if not any(token in str(candidate).lower() for token in ["milestone", "alpha", "beta", "rc"])
            ]
            gradle_candidates = stable_candidates or gradle_candidates
            if gradle_candidates:
                gradle = str(gradle_candidates[-1])
        if gradle is None:
            print("[warn] Gradle wrapper/global gradle not found. Android project generated, APK build skipped.")
            return
        command = [gradle, "assembleDebug"]

    subprocess.run(command, cwd=output_dir, check=True)


def build_android(args: argparse.Namespace) -> None:
    project_dir = Path(args.project).resolve()
    config = read_project(project_dir)
    validate_scene(project_dir, config["entry_scene"])

    # Set VULKAN_SDK environment variable if provided
    if args.vulkan_sdk:
        os.environ["VULKAN_SDK"] = args.vulkan_sdk

    output_dir = Path(args.output).resolve() if args.output else project_dir / "build" / "android"
    if output_dir.exists():
        shutil.rmtree(output_dir)

    copy_tree(ANDROID_TEMPLATE, output_dir)

    replace_text(output_dir / "app" / "build.gradle", {
        'namespace "com.ave.generated"': f'namespace "{config["package"]}"',
        'applicationId "com.ave.generated"': f'applicationId "{config["package"]}"',
        "__AVE_ENGINE_ROOT__": ENGINE_ROOT.as_posix(),
    })
    replace_text(output_dir / "app" / "src" / "main" / "cpp" / "CMakeLists.txt", {
        "__AVE_ENGINE_ROOT__": ENGINE_ROOT.as_posix(),
    })
    replace_text(output_dir / "app" / "src" / "main" / "AndroidManifest.xml", {
        'android:screenOrientation="landscape"': f'android:screenOrientation="{config["orientation"]}"',
        'android:label="Ave Game"': f'android:label="{config["name"]}"',
        "__AVE_ACTIVITY_CLASS__": config["android_activity"],
    })

    package_project_data(project_dir, output_dir)
    package_scripts(project_dir, output_dir, config["package"])
    compile_shaders(project_dir, output_dir)
    write_local_properties(output_dir, args.sdk_dir)

    if not args.no_gradle:
        run_gradle(output_dir)

    print(f"[ok] Android project generated: {output_dir}")
    print(f"[ok] Entry scene: {config['entry_scene']}")


def preview_project(args: argparse.Namespace) -> None:
    project_dir = Path(args.project).resolve()
    config = read_project(project_dir)
    validate_scene(project_dir, config["entry_scene"])

    preview_dir = project_dir / "build" / "preview"
    compiled_shader_dir = preview_dir / "compiled_shaders"
    compile_shaders_to_dir(project_dir, compiled_shader_dir)

    build_dir = Path(args.build_dir).resolve() if args.build_dir else ENGINE_ROOT / "build" / "preview"
    if not args.no_build:
        configure = [
            "cmake",
            "-S",
            str(ENGINE_ROOT),
            "-B",
            str(build_dir),
            "-DAVE_BUILD_EXAMPLES=ON",
        ]
        subprocess.run(configure, check=True)

        build = [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ave_preview",
            "--config",
            args.config,
        ]
        if args.parallel:
            build.extend(["-j", str(args.parallel)])
        subprocess.run(build, check=True)

    candidates = [
        build_dir / args.config / "ave_preview.exe",
        build_dir / args.config / "ave_preview",
        build_dir / "ave_preview.exe",
        build_dir / "ave_preview",
    ]
    executable = next((path for path in candidates if path.exists()), None)
    if executable is None:
        raise RuntimeError(
            "ave_preview executable not found. Ensure CMake found Vulkan and glfw3, "
            "or pass --build-dir pointing at an existing preview build."
        )

    print(f"[ok] Preview project: {project_dir}")
    print(f"[ok] Entry scene: {config['entry_scene']}")
    print(f"[ok] Compiled shaders: {compiled_shader_dir}")

    command = [
        str(executable),
        str(project_dir),
        str(compiled_shader_dir),
        str(args.width),
        str(args.height),
    ]
    subprocess.run(command, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(prog="ave", description="Ave Android Vulkan Engine build tool")
    subparsers = parser.add_subparsers(dest="command")

    build = subparsers.add_parser("build")
    build_sub = build.add_subparsers(dest="platform")

    android = build_sub.add_parser("android")
    android.add_argument("project", help="Path to a game project folder")
    android.add_argument("--output", help="Generated Android project output directory")
    android.add_argument("--sdk-dir", help="Android SDK path used to generate local.properties")
    android.add_argument("--vulkan-sdk", help="Vulkan SDK path (sets VULKAN_SDK environment variable)")
    android.add_argument("--no-gradle", action="store_true", help="Generate Android project without invoking Gradle")
    android.set_defaults(func=build_android)

    preview = subparsers.add_parser("preview", help="Run the desktop PC preview runtime")
    preview.add_argument("project", help="Path to a game project folder")
    preview.add_argument("--build-dir", help="CMake build directory for the preview executable")
    preview.add_argument("--config", default="Debug", help="CMake configuration to build/run")
    preview.add_argument("--width", type=int, default=1280, help="Preview window width")
    preview.add_argument("--height", type=int, default=720, help="Preview window height")
    preview.add_argument("--parallel", type=int, default=8, help="Parallel build jobs")
    preview.add_argument("--no-build", action="store_true", help="Run an existing preview executable without rebuilding")
    preview.set_defaults(func=preview_project)

    args = parser.parse_args()
    if not hasattr(args, "func"):
        parser.print_help()
        return 1

    try:
        args.func(args)
        return 0
    except Exception as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
