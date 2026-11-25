# Fixed embedded icons

The 102 initial PNG assets were recovered byte-for-byte from the verified
Windows DLL built at commit `4401930325adfd94876d02373a73bf4532a91acc`
(workflow run 45620677316). The [manifest](manifest.json) records the resource
ID, file size, SHA-256 and source API field or URL. These are existing Guild Wars
2 assets, not newly generated artwork. See [third-party notices](../../THIRD_PARTY_NOTICES.txt)
for ArenaNet artwork attribution and the GW2Icon crafting mirror.

The build reads local images, checks signatures, hashes, required resource IDs
and paired elite icons, and verifies the actual linked DLL resource bytes in CI.
No fixed icon downloads occur during compilation. Dynamic item/skin/skill
icons still use the runtime cache.

For intentional maintenance, run
`python3 tools/update_embedded_icons.py /path/to/new-staging-directory`.
The staging directory must not exist. Review the images, source URLs, resource
IDs, manifest and licensing before replacing the checked-in images. Do not
overwrite the baseline with a partial or unreviewed download.
