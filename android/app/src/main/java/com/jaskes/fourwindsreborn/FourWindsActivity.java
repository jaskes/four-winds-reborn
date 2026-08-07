package com.jaskes.fourwindsreborn;

import org.libsdl.app.SDLActivity;

/** Android host for the native Four Winds Reborn executable. */
public final class FourWindsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "SDL2_image",
            "SDL2_mixer",
            "SDL2_ttf",
            "main"
        };
    }
}
