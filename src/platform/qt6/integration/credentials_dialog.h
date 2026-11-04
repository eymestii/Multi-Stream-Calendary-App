// Diálogo de registro/verificación de credenciales de aplicación.
// La verificación y el guardado viven en core/integrations; este archivo solo
// presenta los campos y muestra el resultado.
#pragma once
#include <QDialog>
#include <string>

#include "core/integrations/config.h"
#include "qt_platform_services.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class CredentialsDialog : public QDialog {
    Q_OBJECT
public:
    explicit CredentialsDialog(QWidget* parent = nullptr);

    // Puerta de arranque: si las credenciales guardadas siguen verificadas no
    // muestra nada; si no, obliga a verificarlas. Devuelve false si se cancela.
    static bool ensureVerified(QWidget* parent);

protected:
    void reject() override;

private:
    void verifyAndSave();
    void setBusy(bool busy);
    void showError(const QString& text);
    void showInfo(const QString& text);

    QLineEdit* edTwitchId_{};
    QLineEdit* edKickId_{};
    QLineEdit* edKickSecret_{};
    QLineEdit* edKickRedirect_{};
    QCheckBox* chkShowSecret_{};
    QLabel* lblStatus_{};
    QPushButton* btnSave_{};
    QPushButton* btnCancel_{};

    // Se conservan aunque la UI no los edite: Twitch no los usa para device
    // flow, pero pueden venir en la configuración existente.
    std::string twitchSecret_;
    std::string twitchRedirect_;
    QtPlatformServices services_;
    bool busy_ = false;
};
