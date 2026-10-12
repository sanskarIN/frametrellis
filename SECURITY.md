# Security policy

## Supported versions

FrameTrellis has not had a stable release yet. Security fixes are made on the default branch and
included in the next release. Once v1.0.0 is out, the latest minor release receives fixes.

## Reporting a vulnerability

Please **do not** open a public issue for security problems. Instead use one of:

* GitHub's private vulnerability reporting: *Security → Report a vulnerability* in this
  repository;
* the contact form at <https://sanskarin.github.io/contact/#send-a-message>.

Include the affected version, platform, a description of the problem and, if possible, a minimal
proof of concept (for example a crafted project file). Do not send private footage.

You can expect an acknowledgement within a week. We will keep you informed, credit you in the
release notes if you wish, and publish an advisory once a fix is available.

## Scope

Of particular interest:

* Crafted `.ftproj` project files that crash FrameTrellis, read or write unexpected files, or
  execute code.
* Crafted media files that crash FrameTrellis in its own code (crashes inside FFmpeg or MLT
  should also be reported upstream).
* Problems in how FrameTrellis launches its render helper or handles paths.

See [docs/SECURITY.md](docs/SECURITY.md) for the security design.
