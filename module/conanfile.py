from conans import ConanFile


class LibtorrentNode(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = "Libtorrent/1.1.1@joystream/stable"
    generators = "cmake"

    options = {
      "runtime": ["node", "electron"],
      "runtime_version": "ANY"
    }

    def configure(self):
        if self.options.runtime_version == "":
          raise ValueError('Invalid runtime_version value')

        # build static libs
        self.options["Libtorrent"].deprecated_functions=False
        self.options["Libtorrent"].shared=False
        self.options["Boost"].shared=False
        self.options["zlib"].shared=False
        self.options["bzip2"].shared=False
        self.options["OpenSSL"].shared=False

        # with position independent code
        if self.settings.compiler != "Visual Studio":
          self.options["Libtorrent"].fPIC=True
          self.options["Boost"].fPIC=True
          self.options["bzip2"].fPIC=True
