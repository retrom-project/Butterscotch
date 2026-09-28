# Retrom Web candidate

The upstream baseline is `e8294c9070a4fb29a98e6d551fbf773c01214201`.
`retrom-web` retains the host-controlled worker, input and checkpoint ABI;
upstream's independent JSPI `web` backend remains available unchanged.
`retrom-web-meta` retains the metadata parser shipped in the release contract.

The runner uses upstream miniaudio with host-driven stereo PCM output instead
of opening its own audio device. Frame pacing follows upstream's effective game
speed. Checkpoint v2 preserves fractional priority values and the current array
representation. Resources that v2 cannot serialize (including particle pools,
spatial audio emitters, vertex resources and game speed overrides) explicitly
disable checkpoints rather than produce an incomplete save.

Run the source contract, Web host tests and native checkpoint test listed in
`AGENTS.md`, then build with `.github/rpg-runtime/build-candidate.sh` into an empty
directory. Local candidate validation does not promote the remote maintenance
branch or change any released Provider pin.
