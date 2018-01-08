#
# Copyright (c) 2017 Bitprim developers (see AUTHORS)
#
# This file is part of Bitprim.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
from conans import ConanFile, CMake

def option_on_off(option):
    return "ON" if option else "OFF"

class BitprimNodeConan(ConanFile):
    name = "bitprim-node"
    version = "0.5"
    license = "http://www.boost.org/users/license.html"
    url = "https://github.com/bitprim/bitprim-node"
    description = "Bitcoin full node"
    settings = "os", "compiler", "build_type", "arch"

    options = {"shared": [True, False],
               "fPIC": [True, False],
               "with_remote_blockchain": [True, False],
               "with_remote_database": [True, False],
               "with_litecoin": [True, False]
    }
    # "with_tests": [True, False],
    # "with_console": [True, False],
    # "not_use_cpp11_abi": [True, False]

    default_options = "shared=False", \
        "fPIC=True", \
        "with_remote_blockchain=False", \
        "with_remote_database=False", \
        "with_litecoin=False"

    # "with_tests=True", \
    # "with_console=True", \
    # "not_use_cpp11_abi=False"

    with_tests = False
    with_console = True


    generators = "cmake"
    exports_sources = "src/*", "CMakeLists.txt", "cmake/*", "bitprim-nodeConfig.cmake.in", "include/*", "test/*", "console/*"
    package_files = "build/lbitprim-node.a"
    build_policy = "missing"

    requires = (("bitprim-conan-boost/1.64.0@bitprim/stable"),
                ("bitprim-blockchain/0.5@bitprim/testing"),
                ("bitprim-network/0.5@bitprim/testing"))

    def build(self):
        cmake = CMake(self)
        
        cmake.definitions["USE_CONAN"] = option_on_off(True)
        cmake.definitions["NO_CONAN_AT_ALL"] = option_on_off(False)
        cmake.definitions["CMAKE_VERBOSE_MAKEFILE"] = option_on_off(False)
        cmake.definitions["ENABLE_SHARED"] = option_on_off(self.options.shared)
        cmake.definitions["ENABLE_POSITION_INDEPENDENT_CODE"] = option_on_off(self.options.fPIC)

        cmake.definitions["WITH_REMOTE_BLOCKCHAIN"] = option_on_off(self.options.with_remote_blockchain)
        cmake.definitions["WITH_REMOTE_DATABASE"] = option_on_off(self.options.with_remote_database)

        # cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(self.options.not_use_cpp11_abi)
        # cmake.definitions["WITH_TESTS"] = option_on_off(self.options.with_tests)
        # cmake.definitions["WITH_CONSOLE"] = option_on_off(self.options.with_console)

        cmake.definitions["WITH_TESTS"] = option_on_off(self.with_tests)
        cmake.definitions["WITH_CONSOLE"] = option_on_off(self.with_console)

        cmake.definitions["WITH_LITECOIN"] = option_on_off(self.options.with_litecoin)

        #TODO(bitprim): compare with the other project to see if this could be deleted!
        if self.settings.compiler == "gcc":
            if float(str(self.settings.compiler.version)) >= 5:
                cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(False)
            else:
                cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(True)

        cmake.definitions["BITPRIM_BUILD_NUMBER"] = os.getenv('BITPRIM_BUILD_NUMBER', '-')
        cmake.configure(source_dir=self.conanfile_directory)
        cmake.build()

    def imports(self):
        self.copy("*.h", "", "include")

    def package(self):
        self.copy("*.h", dst="include", src="include")
        self.copy("*.hpp", dst="include", src="include")
        self.copy("*.ipp", dst="include", src="include")

        # self.copy("bn.exe", dst="bin", keep_path=False) # Windows
        # self.copy("bn", dst="bin", keep_path=False) # Linux / Macos

        self.copy("bn.exe", dst="bin", src="bin") # Windows
        self.copy("bn", dst="bin", src="bin") # Linux / Macos

        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.dylib*", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.includedirs = ['include']
        self.cpp_info.libs = ["bitprim-node"]
