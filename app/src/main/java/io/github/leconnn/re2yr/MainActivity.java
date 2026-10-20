package io.github.leconnn.re2yr;

import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // The last library is the one whose SDL_main is called.
        return new String[] { "SDL3", "re2yr" };
    }
}
