package org.drnas.freecad.bootstrap;

// Qt 6.8: inherit its native lifecycle/input translation unchanged.
// AndroidManifest selects landscape before Qt creates its first QWindow.
public final class DesktopQtActivity extends org.qtproject.qt.android.bindings.QtActivity {
}
