# Retrom Web candidate

The upstream baseline is `0a36f376270087a229bf268591582af4760607fe`.
`retrom-web` retains the host-controlled worker, input and checkpoint ABI;
upstream's independent JSPI `web` backend remains available unchanged.
`retrom-web-meta` retains the metadata parser shipped in the release contract.

The runner uses upstream miniaudio with host-driven stereo PCM output instead
of opening its own audio device. Frame pacing follows upstream's effective game
speed. Checkpoint v2 preserves fractional priority values and the current array
representation and double-precision instance rotation. Resources that v2 cannot
serialize (including physics worlds and resources, particle pools,
spatial audio emitters, vertex resources, disabled drawing and game speed overrides) explicitly
disable checkpoints rather than produce an incomplete save.

Run the source contract, Web host tests and native checkpoint test listed in
`AGENTS.md`, then build with `.github/rpg-runtime/build-candidate.sh` into an empty
directory. Local candidate validation does not promote the remote maintenance
branch or change any released Provider pin.
