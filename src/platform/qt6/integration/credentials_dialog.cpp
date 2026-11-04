#include "credentials_dialog.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/integrations/credentials.h"
#include "core/paths.h"

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString errorText(const sp::integrations::Result& result) {
    if (result.ok()) return {};
    if (!result.message.empty()) return qs(result.message);
    return QStringLiteral("No se pudieron verificar las credenciales.");
}

}  // namespace

CredentialsDialog::CredentialsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Credenciales de Twitch y Kick"));
    setModal(true);
    setMinimumWidth(560);

    const sp::integrations::IntegrationConfig current =
        sp::integrations::loadIntegrationConfig(sp::defaultConfigPath());
    twitchSecret_ = current.twitch.clientSecret;
    twitchRedirect_ = current.twitch.redirectUri;

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(tr(
        "Cada usuario debe registrar sus propias credenciales de aplicación. "
        "Se verifican contra Twitch y Kick antes de guardarlas; sin esta "
        "verificación la aplicación no se abre. Deja vacía la plataforma que "
        "no uses."));
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* form = new QFormLayout;
    edTwitchId_ = new QLineEdit(qs(current.twitch.clientId));
    edTwitchId_->setPlaceholderText(tr("Client ID de tu app de Twitch"));
    form->addRow(tr("Twitch Client ID"), edTwitchId_);

    edKickId_ = new QLineEdit(qs(current.kick.clientId));
    edKickId_->setPlaceholderText(tr("Client ID de tu app de Kick"));
    form->addRow(tr("Kick Client ID"), edKickId_);

    auto* secretRow = new QHBoxLayout;
    edKickSecret_ = new QLineEdit(qs(current.kick.clientSecret));
    edKickSecret_->setEchoMode(QLineEdit::Password);
    edKickSecret_->setPlaceholderText(tr("Client Secret de tu app de Kick"));
    chkShowSecret_ = new QCheckBox(tr("Mostrar"));
    secretRow->addWidget(edKickSecret_, 1);
    secretRow->addWidget(chkShowSecret_);
    form->addRow(tr("Kick Client Secret"), secretRow);

    edKickRedirect_ = new QLineEdit(qs(current.kick.redirectUri));
    edKickRedirect_->setPlaceholderText(QStringLiteral("http://127.0.0.1:7512/callback"));
    form->addRow(tr("Kick Redirect URI"), edKickRedirect_);
    root->addLayout(form);

    auto* help = new QLabel(tr(
        "La Redirect URI debe coincidir exactamente con la configurada en tu app "
        "de Kick. Las credenciales se guardan solo en este equipo, en %1, con "
        "permisos de lectura exclusivos del propietario (0600 en Linux).")
                            .arg(qs(sp::defaultConfigPath().string())));
    help->setWordWrap(true);
    help->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(help);

    lblStatus_ = new QLabel;
    lblStatus_->setWordWrap(true);
    lblStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(lblStatus_);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    btnCancel_ = new QPushButton(tr("Cancelar"));
    btnSave_ = new QPushButton(tr("Verificar y guardar"));
    btnSave_->setDefault(true);
    buttons->addWidget(btnCancel_);
    buttons->addWidget(btnSave_);
    root->addLayout(buttons);

    connect(chkShowSecret_, &QCheckBox::toggled, this, [this](bool show) {
        edKickSecret_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(btnSave_, &QPushButton::clicked, this, &CredentialsDialog::verifyAndSave);
    connect(btnCancel_, &QPushButton::clicked, this, &CredentialsDialog::reject);
}

bool CredentialsDialog::ensureVerified(QWidget* parent) {
    const sp::integrations::IntegrationConfig current =
        sp::integrations::loadIntegrationConfig(sp::defaultConfigPath());
    if (current.credentialsReady()) return true;

    CredentialsDialog dialog(parent);
    return dialog.exec() == QDialog::Accepted;
}

void CredentialsDialog::verifyAndSave() {
    if (busy_) return;

    const QString twitchId = edTwitchId_->text().trimmed();
    const QString kickId = edKickId_->text().trimmed();
    const QString kickSecret = edKickSecret_->text().trimmed();
    const QString kickRedirect = edKickRedirect_->text().trimmed();

    if (twitchId.isEmpty() && kickId.isEmpty()) {
        showError(tr("Introduce al menos el Client ID de Twitch o el de Kick."));
        return;
    }
    if (!kickId.isEmpty() && kickSecret.isEmpty()) {
        showError(tr("Kick necesita su Client Secret para verificar la aplicación."));
        return;
    }
    if (kickId.isEmpty() && !kickSecret.isEmpty()) {
        showError(tr("Has escrito un Client Secret de Kick sin su Client ID."));
        return;
    }
    if (!kickId.isEmpty() && kickRedirect.isEmpty()) {
        showError(tr("Indica la Redirect URI de Kick."));
        return;
    }
    if (!kickId.isEmpty() && !sp::integrations::isLoopbackRedirect(kickRedirect.toStdString())) {
        showError(tr("La Redirect URI de Kick debe apuntar a 127.0.0.1 o localhost con un puerto, "
                     "por ejemplo http://127.0.0.1:7512/callback."));
        return;
    }

    sp::integrations::IntegrationConfig cfg;
    cfg.twitch.clientId = twitchId.toStdString();
    cfg.twitch.clientSecret = twitchSecret_;
    cfg.twitch.redirectUri = twitchRedirect_;
    cfg.kick.clientId = kickId.toStdString();
    cfg.kick.clientSecret = kickSecret.toStdString();
    cfg.kick.redirectUri = kickRedirect.toStdString();

    setBusy(true);
    showInfo(tr("Verificando credenciales con Twitch y Kick..."));

    sp::integrations::verifyCredentials(
        cfg, services_.http(),
        [this, cfg](sp::integrations::CredentialVerification verification) mutable {
            setBusy(false);
            if (!verification.ok()) {
                QStringList messages;
                const QString twitchError = errorText(verification.twitch);
                const QString kickError = errorText(verification.kick);
                if (!twitchError.isEmpty()) messages << twitchError;
                if (!kickError.isEmpty()) messages << kickError;
                if (messages.isEmpty()) messages << tr("No se pudieron verificar las credenciales.");
                showError(messages.join(QStringLiteral("\n")));
                return;
            }

            sp::integrations::markVerified(cfg);
            if (!cfg.credentialsReady()) {
                showError(tr("La configuración no es válida. Revisa el Client ID y la Redirect URI "
                             "de Kick."));
                return;
            }
            if (!sp::integrations::saveIntegrationConfig(sp::defaultConfigPath(), cfg)) {
                showError(tr("Las credenciales son válidas, pero no se pudo guardar el archivo. "
                             "Revisa los permisos de la carpeta de configuración."));
                return;
            }
            accept();
        });
}

void CredentialsDialog::setBusy(bool busy) {
    busy_ = busy;
    edTwitchId_->setEnabled(!busy);
    edKickId_->setEnabled(!busy);
    edKickSecret_->setEnabled(!busy);
    edKickRedirect_->setEnabled(!busy);
    chkShowSecret_->setEnabled(!busy);
    btnSave_->setEnabled(!busy);
    btnSave_->setText(busy ? tr("Verificando...") : tr("Verificar y guardar"));
}

void CredentialsDialog::showError(const QString& text) {
    lblStatus_->setStyleSheet(QStringLiteral("color: #b00020;"));
    lblStatus_->setText(text);
}

void CredentialsDialog::showInfo(const QString& text) {
    lblStatus_->setStyleSheet(QStringLiteral("color: #1b5e20;"));
    lblStatus_->setText(text);
}

void CredentialsDialog::reject() {
    // Si se cierra durante la verificación, los callbacks pendientes no deben
    // llegar a un diálogo destruido.
    services_.http().cancelAll();
    QDialog::reject();
}
