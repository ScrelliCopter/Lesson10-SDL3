#!/usr/bin/env python3

from pathlib import Path
import subprocess
import platform


def compile_metal_shaders(sources: [str], library: str, cflags: [str]=None, sdk="macosx", debug: bool=False):
	if cflags is None:
		cflags = []

	def xcrun_find_program(name: str):
		return subprocess.run(["xcrun", "-sdk", sdk, "-f", name],
			capture_output=True, check=True).stdout.decode("utf-8").strip()

	# Find Metal compilers
	metal = xcrun_find_program("metal")
	if debug:
		metal_dsymutil = xcrun_find_program("metal-dsymutil")
	else:
		metallib = xcrun_find_program("metallib")

	# Compile each source to an AIR (Apple Intermediate Representation)
	cflags.append("-frecord-sources")
	air_objects = [f"{s.removesuffix(".metal")}.air" for s in sources]
	for src, obj in zip(sources, air_objects):
		subprocess.run([metal, *cflags, "-c", src, "-o", obj], check=True)

	# Build the Metal library
	if debug:
		subprocess.run([metal, "-frecord-sources", "-o", library, *air_objects], check=True)
		subprocess.run([metal_dsymutil, "-flat", "-remove-source", library], check=True)
	else:
		subprocess.run([metallib, "-o", library, *air_objects])

	# Clean up AIR objects
	for obj in air_objects:
		Path(obj).unlink()


def compile_shaders():
	system = platform.system()
	if system == "Darwin":
		compile_platform = "macos"
		sdk_platform = "macosx"
		min_version = "10.11"
		compile_metal_shaders(
			sources=["Shader.vertex.metal", "Shader.fragment.metal"],
			library="Data/Shader.metallib",
			cflags=["-Wall", "-O3",
				f"-std={compile_platform}-metal1.1",
				f"-m{sdk_platform}-version-min={min_version}"],
			sdk=sdk_platform)


if __name__ == "__main__":
	compile_shaders()
