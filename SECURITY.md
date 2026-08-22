# Security Policy

[中文](SECURITY.zh-CN.md)

## Supported versions

Until 1.0, only the latest tagged minor release and the current default branch receive
security fixes. Version 0.1.x is a research preview and must be evaluated against the
limitations in `docs/security-model.md`.

## Reporting a vulnerability

Do not open a public issue for an exploitable parser/writer flaw, protected-output code
execution defect, unsafe publication bug or accidental secret disclosure. Use the hosting
platform's private security advisory feature. Before publishing the repository, configure
a monitored security contact in the repository settings and replace this paragraph with
that address if email reporting is desired.

Include affected version/commit, host and target architecture, toolchain, minimal
reproducer, command/seed, expected impact and whether the artifact may be shared. Do not
send production secrets or third-party proprietary binaries.

The project aims to acknowledge a complete report within 7 days, assess severity within
14 days, and coordinate a fix/advisory. These are best-effort targets for a community
project, not a service-level agreement.

Security claims about resistance to reverse engineering are research discussions rather
than vulnerabilities unless they contradict a documented guarantee. Memory corruption,
unsafe ELF publication and semantic miscompilation are security-relevant defects.
