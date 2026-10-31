// SDL3 replacement for the original XInput polling implementation.
void CPad::Update() {
    GInputSDL3_Update();
    GInputSDL3State state{};
    GInputSDL3_GetState(Index, &state, sizeof(state));
    OldState = NewState;
    NewState.Clear();
    TempState.Clear();
    PadConnected = state.connected != 0;
    if (!PadConnected) return;
    NewState.LeftStickX = static_cast<short>(state.leftX * 255);
    NewState.LeftStickY = static_cast<short>(state.leftY * 255);
    NewState.RightStickX = static_cast<short>(state.rightX * 255);
    NewState.RightStickY = static_cast<short>(state.rightY * 255);
    NewState.LeftShoulder2 = (state.buttons & (1u << GINPUT_LEFT_TRIGGER))
        ? static_cast<short>(state.leftTrigger * 255) : 0;
    NewState.RightShoulder2 = (state.buttons & (1u << GINPUT_RIGHT_TRIGGER))
        ? static_cast<short>(state.rightTrigger * 255) : 0;
    short* buttons[] = {
        &NewState.DPadUp, &NewState.DPadDown, &NewState.DPadLeft, &NewState.DPadRight,
        &NewState.Start, &NewState.Select, &NewState.LeftShock, &NewState.RightShock,
        &NewState.LeftShoulder1, &NewState.RightShoulder1,
        &NewState.Cross, &NewState.Circle, &NewState.Square, &NewState.Triangle
    };
    for (unsigned i = 0; i < 14; ++i)
        *buttons[i] = (state.buttons & (1u << i)) ? 255 : 0;
    if (ShakeDur > 0) {
        GInputSDL3_Rumble(Index, static_cast<unsigned char>(ShakeFreq) / 255.0f, 0.0f, ShakeDur);
        ShakeDur = 0;
    }
}
