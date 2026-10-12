# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Unit tests for packaging/windows/package.py that run on any platform.

    python3 -m unittest discover -s packaging/tests -v

The license extraction tests need the zstd and git command-line tools and are skipped
without them. They build small source packages locally and never use the network.
"""

from __future__ import annotations

import importlib.util
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]


def load_package_module():
    spec = importlib.util.spec_from_file_location("package", ROOT / "packaging/windows/package.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["package"] = module  # dataclasses look the module up while it is being loaded
    spec.loader.exec_module(module)
    return module


package = load_package_module()


def tar_bytes(files: dict[str, bytes], mode: str = "w:gz", extra=()) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode=mode) as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        for info in extra:
            archive.addfile(info)
    return buffer.getvalue()


class VersionTest(unittest.TestCase):
    def test_reads_the_version_of_this_repository(self):
        self.assertRegex(package.read_version(ROOT), r"^\d+\.\d+\.\d+$")

    def test_version_may_follow_on_another_line(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\n"
                "project(FrameTrellis\n    VERSION 2.3.4\n    LANGUAGES CXX)\n")
            self.assertEqual(package.read_version(Path(directory)), "2.3.4")


class PackageDatabaseTest(unittest.TestCase):
    def test_source_archive_replaces_the_epoch_separator(self):
        entry = package.Package("mingw-w64-ucrt-x86_64-x", "1:2.0-3", "mingw-w64-x", "", "", [])
        self.assertEqual(entry.source_archive, "mingw-w64-x-1~2.0-3.src.tar.zst")

    def test_spdx_license_needs_every_license_in_spdx_form(self):
        def make(licenses):
            return package.Package("p", "1-1", "p", "", "", licenses)

        self.assertEqual(make(["spdx:MIT"]).spdx_license, "MIT")
        self.assertEqual(make(["spdx:MIT", "spdx:GPL-2.0-or-later OR LGPL-2.1-or-later"])
                         .spdx_license, "MIT AND (GPL-2.0-or-later OR LGPL-2.1-or-later)")
        self.assertEqual(make(["spdx:MIT", "custom"]).spdx_license, "NOASSERTION")
        self.assertEqual(make([]).spdx_license, "NOASSERTION")

    def test_reads_the_local_pacman_database(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = root / "var/lib/pacman/local/mingw-w64-ucrt-x86_64-zlib-1.3-1"
            entry.mkdir(parents=True)
            (entry / "desc").write_text(
                "%NAME%\nmingw-w64-ucrt-x86_64-zlib\n\n%VERSION%\n1.3-1\n\n"
                "%BASE%\nmingw-w64-zlib\n\n%DESC%\nCompression library\n\n"
                "%URL%\n\n%LICENSE%\nspdx:Zlib\n\n")
            (entry / "files").write_text(
                "%FILES%\nucrt64/\nucrt64/bin/\nucrt64/bin/zlib1.dll\n"
                "ucrt64/share/licenses/zlib/LICENSE\n\n")
            packages, owners = package.read_package_database(root)

        zlib = packages["mingw-w64-ucrt-x86_64-zlib"]
        self.assertEqual((zlib.version, zlib.base, zlib.url), ("1.3-1", "mingw-w64-zlib", ""))
        self.assertEqual(zlib.licenses, ["spdx:Zlib"])
        self.assertEqual(owners["ucrt64/bin/zlib1.dll"], zlib.name)
        self.assertNotIn("ucrt64/bin/", owners)


class LicensePathTest(unittest.TestCase):
    def test_accepts_license_files_near_the_top(self):
        for name, expected in [
            ("ffmpeg-9.0/COPYING.LGPLv2.1", "COPYING.LGPLv2.1"),
            ("nettle-4.0/COPYINGv3", "COPYINGv3"),
            ("icu/LICENSE", "LICENSE"),
            ("lame-3.100/debian/copyright", "debian/copyright"),
            ("openal-soft/fmt-11.2.0/LICENSE", "fmt-11.2.0/LICENSE"),
            ("opencore-amr/opencore/NOTICE", "opencore/NOTICE"),
            ("project/LICENSES/MIT.txt", "LICENSES/MIT.txt"),
            ("project/Licence.md", "Licence.md"),
        ]:
            with self.subTest(name=name):
                self.assertEqual(package.license_path(name), PurePosixPath(expected))

    def test_rejects_other_files_and_unsafe_names(self):
        for name in ["project/src/main.c", "project/a/b/LICENSE", "project/COPYING.c/x",
                     "project/../COPYING", "/etc/COPYING", "C:/project/COPYING",
                     "COPYING", "project/", "project/LICENSEE"]:
            with self.subTest(name=name):
                self.assertIsNone(package.license_path(name))

    def test_git_trees_have_no_top_level_folder(self):
        self.assertEqual(package.license_path("COPYING", strip_top=False),
                         PurePosixPath("COPYING"))
        self.assertEqual(package.license_path("librtmp/COPYING", strip_top=False),
                         PurePosixPath("librtmp/COPYING"))


class GitSourcesTest(unittest.TestCase):
    def test_reads_the_revision_of_each_git_source(self):
        srcinfo = """pkgbase = mingw-w64-x
\tpkgver = 1.0
\tsource = x264::git+https://code.videolan.org/videolan/x264.git#commit=b35605a
\tsource = git+https://git.ffmpeg.org/rtmpdump#tag=v2.6
\tsource = other::git+https://example.org/other.git#branch=stable
\tsource = plain::git+https://example.org/plain.git
\tsource = 0001-a+b.patch
\tsource = https://example.org/archive-1.0.tar.gz
\tsha256sums = SKIP
"""
        self.assertEqual(package.git_sources(srcinfo), {
            "x264": "b35605a",
            "rtmpdump": "v2.6",
            "other": "refs/heads/stable",
            "plain": "HEAD",
        })


@unittest.skipUnless(shutil.which("zstd"), "needs the zstd tool")
class LicenseExtractorTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.target = self.root / "licenses"
        self.entry = package.Package("mingw-w64-ucrt-x86_64-demo", "1.0-1", "mingw-w64-demo",
                                     "", "", ["custom"])

    def tearDown(self):
        self.directory.cleanup()

    def write_source_package(self, files: dict[str, bytes]) -> None:
        """Stores a source package in the cache so that nothing is downloaded."""
        tar = self.root / "source.tar"
        tar.write_bytes(tar_bytes(files, mode="w"))
        subprocess.run(["zstd", "--quiet", "--force", "-o",
                        str(self.cache / self.entry.source_archive), str(tar)], check=True)

    def extract(self) -> set[str]:
        found = package.LicenseExtractor(self.cache).extract(self.entry, self.target)
        return {str(path) for path in found}

    def test_copies_license_files_from_release_archives(self):
        upstream = tar_bytes({
            "demo-1.0/COPYING": b"GPL text",
            "demo-1.0/COPYING.LESSERv3": b"LGPL text",
            "demo-1.0/third/LICENSE": b"MIT text",
            "demo-1.0/third/deeper/LICENSE": b"too deep",
            "demo-1.0/src/main.c": b"int main() {}",
            "demo-1.0/../escape/COPYING": b"outside",
            "/absolute/COPYING": b"outside",
        })
        zipped = io.BytesIO()
        with zipfile.ZipFile(zipped, "w") as archive:
            archive.writestr("extra-2.0/LICENSE.txt", "BSD text")
            archive.writestr("extra-2.0/COPYING", "GPL text, again")
        self.write_source_package({
            "mingw-w64-demo/PKGBUILD": b"pkgname=demo",
            "mingw-w64-demo/.SRCINFO": b"pkgbase = mingw-w64-demo\n",
            "mingw-w64-demo/demo-1.0.tar.gz": upstream,
            "mingw-w64-demo/demo-1.0.tar.gz.sig": b"signature",
            "mingw-w64-demo/extra-2.0.zip": zipped.getvalue(),
            "mingw-w64-demo/0001-fix.patch": b"--- a\n+++ b\n",
        })

        found = self.extract()

        self.assertEqual(found, {"COPYING", "COPYING.LESSERv3", "third/LICENSE", "LICENSE.txt",
                                 "extra-2.0.zip/COPYING"})
        self.assertEqual((self.target / "COPYING").read_bytes(), b"GPL text")
        self.assertEqual((self.target / "extra-2.0.zip/COPYING").read_bytes(),
                         b"GPL text, again")
        written = sorted(p.relative_to(self.root).as_posix()
                         for p in self.root.rglob("*") if p.is_file())
        self.assertFalse([p for p in written if "escape" in p or "absolute" in p], written)

    def test_skips_links_and_very_large_files(self):
        link = tarfile.TarInfo("demo-1.0/LICENSE")
        link.type = tarfile.SYMTYPE
        link.linkname = "/etc/passwd"
        upstream = tar_bytes({
            "demo-1.0/COPYING": b"x" * (package.MAX_LICENSE_FILE_SIZE + 1),
            "demo-1.0/NOTICE": b"notice",
        }, extra=[link])
        self.write_source_package({"mingw-w64-demo/demo-1.0.tar.gz": upstream})
        self.assertEqual(self.extract(), {"NOTICE"})

    @unittest.skipUnless(shutil.which("git"), "needs git")
    def test_reads_git_sources_at_the_built_revision(self):
        work = self.root / "work"
        work.mkdir()

        def git(*arguments: str) -> str:
            return subprocess.run(["git", "-C", str(work), "-c", "user.name=Test",
                                   "-c", "user.email=test@example.invalid", *arguments],
                                  check=True, capture_output=True, text=True).stdout.strip()

        git("init", "--quiet")
        (work / "COPYING").write_text("license at the built revision")
        (work / "lib").mkdir()
        (work / "lib" / "COPYING").write_text("library license")
        git("add", ".")
        git("commit", "--quiet", "-m", "first")
        built = git("rev-parse", "HEAD")
        (work / "COPYING").write_text("a later license")
        git("commit", "--quiet", "-am", "second")

        bare = self.root / "demo"
        subprocess.run(["git", "clone", "--quiet", "--mirror", str(work), str(bare)], check=True)
        files = {f"mingw-w64-demo/demo/{p.relative_to(bare).as_posix()}": p.read_bytes()
                 for p in bare.rglob("*") if p.is_file()}
        files["mingw-w64-demo/.SRCINFO"] = (
            f"pkgbase = mingw-w64-demo\n\tsource = demo::git+https://example.org/demo.git"
            f"#commit={built}\n").encode()
        self.write_source_package(files)

        self.assertEqual(self.extract(), {"COPYING", "lib/COPYING"})
        self.assertEqual((self.target / "COPYING").read_text(), "license at the built revision")


if __name__ == "__main__":
    unittest.main()
