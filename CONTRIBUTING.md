# Contributing

Contributions are welcome. Keep changes focused, explain the user-facing benefit and use the existing architecture and UI conventions.

## Before making changes

- Discuss substantial feature, persistence, network or UI changes before implementing them.
- Branch from the intended target. The current experimental branch is `experiment/composable-loadouts`; do not assume its data model is compatible with the stable branch.
- Read [AGENTS.md](AGENTS.md) and the relevant linked developer reference. Preserve original library import compatibility, local data, existing refresh semantics and the Nexus ImGui 1.80 ABI.
- Do not include API keys, account data, private logs, local databases or generated DLLs in source contributions.

## Validate changes

Run the existing offline regression suite:

```sh
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests --parallel 2
ctest --test-dir build-tests --output-on-failure
```

Extend the relevant regression cases. For runtime changes, verify the Windows x64 UCRT workflow and perform relevant in-game checks. State clearly which checks were performed and which remain outstanding. Follow [Regression testing](docs/regression-testing.md) for details.

## Submit changes

Keep commits reviewable and document behavior changes where appropriate. Include a concise description, test evidence and screenshots for visual changes. Preserve the existing [MIT license](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.txt); contributions are submitted under the project license unless explicitly agreed otherwise.

For security-sensitive reports, follow [SECURITY.md](SECURITY.md), not a public issue.
