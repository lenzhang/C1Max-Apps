# MoonPilot

Native 800 × 340 LVGL interface for C1 Max. Guided by ui-ux-pro-max's compact productivity / remote desktop recommendations, adapted to this framebuffer device.

- Four stable destinations: desktop, hosts, voice, settings. Header reserves 264 px for connection state and cancellation.
- Graphite `#111823`, raised panels `#1c2a3c`, primary text `#eef5fa`, secondary text `#a7bac9`, ice-blue actions `#9bbcff`.
- Noto Sans SC at 18 px; secondary labels 16 px; app title 24 px. No on-screen keyboard. Touch fields show a distinct focus border.
- Every action is at least 44 px tall. Host address and port are labeled; PIN instructions wrap rather than overflowing a status badge.
- Remote frames fit completely inside the preview. Click mapping excludes letterboxing. AI receives the original decoded frame, independently of preview size.
- Local power key returns to launcher. Return cancels the current operation / releases input, then navigates. AI is opt-in through single-step or a bounded ten-step run; manual input interrupts automation.
- No camera, calibration, USB role switching or HID daemon in this application. Voice retains text when TTS fails.
- Screenshots only represent real device rendering; protocol fixtures are documented as fixtures, never real Sunshine validation.
