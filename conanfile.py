from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy
import os

class AithenaConan(ConanFile):
    name        = "aithena"
    version     = "1.0.0"
    license     = "MIT"
    url         = "https://github.com/sa-aris/aithena"
    homepage    = "https://github.com/sa-aris/aithena"
    description = ("C++17 NPC AI framework — FSM, behavior trees, emotions, "
                   "memory, pathfinding, relationships, Lua scripting, "
                   "and C API for Unity/Unreal/Godot")
    topics      = ("gamedev", "npc", "ai", "game-ai", "cpp17")
    settings    = "os", "compiler", "build_type", "arch"
    options     = {"lua_bridge": [True, False]}
    default_options = {"lua_bridge": False}

    def requirements(self):
        if self.options.lua_bridge:
            self.requires("lua/5.4.6")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["NPC_LUA_BRIDGE"] = self.options.lua_bridge
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE",   src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "*.hpp",     src=os.path.join(self.source_folder, "include"),
             dst=os.path.join(self.package_folder, "include"))
        copy(self, "*.h",       src=os.path.join(self.source_folder, "include"),
             dst=os.path.join(self.package_folder, "include"))
        copy(self, "*.a",       src=self.build_folder,
             dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.lib",     src=self.build_folder,
             dst=os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["npc_lib"]
        self.cpp_info.set_property("cmake_target_name", "aithena::aithena")
