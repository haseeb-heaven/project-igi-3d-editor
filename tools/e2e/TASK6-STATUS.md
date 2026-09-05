# Graph and animation workflow checkpoint

The 336 scenarios are draft live workflows: 295 graph cases and 41 animation
representatives across levels 1 through 14. Offline manifest validation passed,
but this does not establish successful UI execution.

The visible level 1 graph smoke currently fails node selection and the graph
edit assertion. Its automatic restoration verified the original graph hash.
Animation button coordinates, anonymous-task selection, save/reload coverage,
and patrol behavior still require implementation or live validation.

Do not interpret action names or successful input dispatch as semantic success.
The native focused run used the existing Release test executable: 66 tests
passed and one live integration test was skipped; two additional RuntimeAiTest
waypoint tests failed in the broader run.
