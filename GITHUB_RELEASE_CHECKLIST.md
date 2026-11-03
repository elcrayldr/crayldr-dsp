# GitHub release checklist — CRAY LDR VOX LEVELER

## Before publishing
- [ ] Choose repository visibility: private or public.
- [ ] Choose and add a project license. Do not publish without an explicit licensing decision.
- [ ] Confirm the repository owner/name and default branch.
- [ ] Confirm the GitHub Actions Windows build succeeds.
- [ ] Confirm pluginval strictness level 5 succeeds. This is a required gate, not an optional warning.
- [ ] Download the VST3 artifact from Actions and install it in a Windows test machine.
- [ ] Test in FL Studio 26 and Cubase 13: scan, instantiate, analyze, apply, bypass, save/reopen, seek, loop, offline render.
- [ ] Compare real-time playback and offline render output.
- [ ] Do not commit private vocal recordings, processed vocal renders, or per-user analysis curves.

## Release policy
A successful DSP test suite is not equivalent to a validated VST3. Do not tag a stable release until the Windows VST3 build, pluginval, and DAW smoke tests have passed.
