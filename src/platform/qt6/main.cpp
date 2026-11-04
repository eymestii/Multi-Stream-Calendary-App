// Stream Planner - punto de entrada Qt6 (Linux).
// Lógica de datos: src/core   |   Interfaz: src/platform/qt6
#include <QApplication>
#include <QIcon>
#include <QMessageBox>

#include "core/planner.h"
#include "integration/credentials_dialog.h"
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("StreamPlanner"));
    QApplication::setApplicationDisplayName(QStringLiteral("Stream Planner"));
    QApplication::setApplicationVersion(QStringLiteral(SP_VERSION));
    // Debe coincidir con el nombre del .desktop (sin extensión): Wayland lo usa para el icono.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.mestisexo.StreamPlanner"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.svg")));

    // Puerta de credenciales: cada usuario debe registrar y verificar las suyas
    // antes de acceder a la interfaz principal.
    if (!CredentialsDialog::ensureVerified(nullptr)) return 0;

    sp::Planner planner(sp::defaultDataPaths());
    if (!planner.open()) {
        QMessageBox::critical(nullptr, QStringLiteral("Stream Planner"),
                              QObject::tr("No se pudo abrir la base de datos. Tus datos no se han eliminado. "
                                          "La aplicación se cerrará para evitar sobrescribirlos."));
        return 1;
    }

    MainWindow w(planner);
    w.show();
    return app.exec();
}
