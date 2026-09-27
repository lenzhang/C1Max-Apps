# HIDPilot visual and interaction system

- Native LVGL at 800 × 340, landscape; physical keyboard, no on-screen keyboard.
- Graphite canvas `#101922`, raised panels `#1b2a36`, near-white `#eef5fa`, muted text `#a7bac9`, mint action accent `#5eead4`.
- Noto Sans SC at 18 px for controls, 16 px for supporting copy, 24 px titles. Buttons are at least 44 px high.
- Header navigation: Agent, Keyboard/Mouse, Voice, Settings; a wide USB connection/status button stays at the right.
- Agent: large live camera preview, separate task field and action area. Calibration is session-local and has explicit corner order and manual focus controls.
- Voice: scrollable transcript, physical text input alternative, large start/finish recording control, stop and replay. Loading states distinguish recognition, dialogue and synthesis.
- Settings: independent vision, conversation, ASR and TTS configurations. Tokens are masked. Service failures remain readable without exposing response bodies or credentials.
- Return stops work; power returns to launcher. USB mode is opt-in each session and restores original functions on exit.
- Launcher artwork: built-in imagegen, real alpha transparency; prompt recorded in `launcher/assets/hidpilot-prompt.json`.
