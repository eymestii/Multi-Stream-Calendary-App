#include "publishpanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "credentials_dialog.h"

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
QStringList qsl(const std::vector<std::string>& v) {
    QStringList out;
    for (const auto& s : v) out << qs(s);
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------------------
// CategoryModel
// ---------------------------------------------------------------------------------------

CategoryModel::CategoryModel(IntegrationController& controller, int platform, QObject* parent)
    : QAbstractListModel(parent), controller_(controller), platform_(platform) {
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(300);
    connect(debounce_, &QTimer::timeout, this, [this] { startRequest(query_, {}, false); });

    connect(&controller_, &IntegrationController::categoriesReady, this,
            [this](int platform, int requestId, const QStringList& ids, const QStringList& names,
                   const QString& cursor) {
                if (platform != platform_ || requestId != pendingId_) return;
                const int n = static_cast<int>(std::min(ids.size(), names.size()));
                if (append_) {
                    if (n > 0) {
                        beginInsertRows(QModelIndex(), items_.size(), items_.size() + n - 1);
                        for (int i = 0; i < n; ++i) items_.push_back({ids[i], names[i]});
                        endInsertRows();
                    }
                } else {
                    beginResetModel();
                    items_.clear();
                    for (int i = 0; i < n; ++i) items_.push_back({ids[i], names[i]});
                    endResetModel();
                }
                cursor_ = cursor;
                hasMore_ = !cursor.isEmpty();
                pending_ = false;
                append_ = false;
            });
    connect(&controller_, &IntegrationController::categoriesFailed, this,
            [this](int platform, int requestId, const QString& message) {
                if (platform != platform_ || requestId != pendingId_) return;
                pending_ = false;
                append_ = false;
                emit failed(message);
            });
}

int CategoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : items_.size();
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
    if (role == Qt::DisplayRole) return items_[index.row()].name;
    if (role == Qt::UserRole) return items_[index.row()].id;
    return {};
}

bool CategoryModel::canFetchMore(const QModelIndex& parent) const {
    return !parent.isValid() && hasMore_ && !pending_;
}

void CategoryModel::fetchMore(const QModelIndex& parent) {
    if (parent.isValid() || !hasMore_ || pending_) return;
    startRequest(query_, cursor_, true);
}

void CategoryModel::scheduleSearch(const QString& text) {
    query_ = text.trimmed();
    debounce_->start();
}

void CategoryModel::refresh() {
    debounce_->stop();
    startRequest(query_, {}, false);
}

void CategoryModel::startRequest(const QString& query, const QString& cursor, bool append) {
    append_ = append;
    pending_ = true;
    pendingId_ = ++requestId_;
    if (!append) {
        beginResetModel();
        items_.clear();
        endResetModel();
        cursor_.clear();
        hasMore_ = false;
    }
    controller_.searchCategories(platform_, pendingId_, query, cursor);
}

QString CategoryModel::idAt(int row) const {
    return (row >= 0 && row < items_.size()) ? items_[row].id : QString();
}

QString CategoryModel::nameAt(int row) const {
    return (row >= 0 && row < items_.size()) ? items_[row].name : QString();
}

// ---------------------------------------------------------------------------------------
// ProviderPanel
// ---------------------------------------------------------------------------------------

ProviderPanel::ProviderPanel(IntegrationController& controller, int platform, QWidget* parent)
    : QWidget(parent), controller_(controller), platform_(platform), caps_(controller.capabilities(platform)) {
    buildUi();
    connectSignals();
    updateLimits();
    updateAuth(static_cast<int>(sp::integrations::AuthState::Disconnected), {},
               controller.configurationProblem(platform));
    updateCounts();
}

void ProviderPanel::buildUi() {
    auto* v = new QVBoxLayout(this);

    auto* authRow = new QHBoxLayout;
    lblStatus_ = new QLabel;
    lblStatus_->setWordWrap(true);
    lblStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    btnAuth_ = new QPushButton(tr("Conectar"));
    btnRefresh_ = new QPushButton(tr("Leer canal"));
    authRow->addWidget(lblStatus_, 1);
    authRow->addWidget(btnRefresh_);
    authRow->addWidget(btnAuth_);
    v->addLayout(authRow);

    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(new QLabel(tr("Título oficial")));
    lblTitleCount_ = new QLabel;
    titleRow->addStretch(1);
    titleRow->addWidget(lblTitleCount_);
    v->addLayout(titleRow);
    edTitle_ = new QLineEdit;
    v->addWidget(edTitle_);

    auto* opts = new QHBoxLayout;
    chkTitle_ = new QCheckBox(tr("Aplicar título"));
    chkCategory_ = new QCheckBox(tr("Aplicar categoría"));
    chkTags_ = new QCheckBox(tr("Aplicar etiquetas"));
    chkTitle_->setChecked(true);
    chkCategory_->setChecked(true);
    chkTags_->setChecked(true);
    btnPlanTitle_ = new QPushButton(tr("Usar título del plan"));
    opts->addWidget(chkTitle_);
    opts->addWidget(chkCategory_);
    opts->addWidget(chkTags_);
    opts->addStretch(1);
    opts->addWidget(btnPlanTitle_);
    v->addLayout(opts);

    v->addWidget(new QLabel(tr("Categoría oficial (busca y elige)")));
    edSearch_ = new QLineEdit;
    edSearch_->setPlaceholderText(tr("Buscar categoría..."));
    v->addWidget(edSearch_);
    listCategories_ = new QListView;
    listCategories_->setMinimumHeight(120);
    listCategories_->setMaximumHeight(180);
    categories_ = new CategoryModel(controller_, platform_, this);
    listCategories_->setModel(categories_);
    v->addWidget(listCategories_);
    lblCategory_ = new QLabel(tr("Categoría: (sin seleccionar)"));
    lblCategory_->setWordWrap(true);
    v->addWidget(lblCategory_);

    v->addWidget(new QLabel(tr("Etiquetas del canal")));
    auto* tagRow = new QHBoxLayout;
    edTag_ = new QLineEdit;
    edTag_->setPlaceholderText(tr("Nueva etiqueta"));
    btnAddTag_ = new QPushButton(tr("Añadir"));
    tagRow->addWidget(edTag_, 1);
    tagRow->addWidget(btnAddTag_);
    v->addLayout(tagRow);
    auto* tagListRow = new QHBoxLayout;
    listTags_ = new QListWidget;
    listTags_->setMaximumHeight(100);
    btnRemoveTag_ = new QPushButton(tr("Quitar"));
    tagListRow->addWidget(listTags_, 1);
    tagListRow->addWidget(btnRemoveTag_);
    v->addLayout(tagListRow);
    lblTagCount_ = new QLabel;
    v->addWidget(lblTagCount_);

    auto* pubRow = new QHBoxLayout;
    btnPublish_ = new QPushButton(tr("Publicar"));
    lblResult_ = new QLabel;
    lblResult_->setWordWrap(true);
    pubRow->addWidget(btnPublish_);
    pubRow->addWidget(lblResult_, 1);
    v->addLayout(pubRow);

    if (!caps_.writeTags) {
        chkTags_->setChecked(false);
        chkTags_->setEnabled(false);
        edTag_->setEnabled(false);
        btnAddTag_->setEnabled(false);
        btnRemoveTag_->setEnabled(false);
        listTags_->setEnabled(false);
    }
}

void ProviderPanel::connectSignals() {
    connect(btnAuth_, &QPushButton::clicked, this, [this] {
        if (connected_) controller_.disconnectPlatform(platform_);
        else if (connecting_) controller_.cancelPlatform(platform_);
        else controller_.connectPlatform(platform_);
    });
    connect(btnRefresh_, &QPushButton::clicked, this, [this] {
        controller_.refreshChannel(platform_);
    });
    connect(edTitle_, &QLineEdit::textEdited, this, [this] {
        dirtyTitle_ = true;
        updateCounts();
    });
    connect(edTitle_, &QLineEdit::textChanged, this, [this] {
        if (!loading_) updateCounts();
    });
    connect(edTitle_, &QLineEdit::editingFinished, this, [this] { emitTarget({}, false); });
    connect(btnPlanTitle_, &QPushButton::clicked, this, [this] {
        loading_ = true;
        edTitle_->setText(planTitle_);
        loading_ = false;
        dirtyTitle_ = false;
        updateCounts();
        emitTarget({}, false);
    });
    connect(edSearch_, &QLineEdit::textEdited, this, [this](const QString& text) {
        categories_->scheduleSearch(text);
    });
    connect(edSearch_, &QLineEdit::returnPressed, this, [this] { categories_->refresh(); });
    connect(listCategories_, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) selectCategory(index.row());
    });
    connect(edTag_, &QLineEdit::returnPressed, this, &ProviderPanel::addTag);
    connect(btnAddTag_, &QPushButton::clicked, this, &ProviderPanel::addTag);
    connect(btnRemoveTag_, &QPushButton::clicked, this, &ProviderPanel::removeTag);
    connect(btnPublish_, &QPushButton::clicked, this, &ProviderPanel::publish);
    connect(categories_, &CategoryModel::failed, this, [this](const QString& message) {
        setStatus(message, true);
    });

    connect(&controller_, &IntegrationController::authChanged, this,
            [this](int platform, int state, const QString& display, const QString& message) {
                if (platform == platform_) updateAuth(state, display, message);
            });
    connect(&controller_, &IntegrationController::authPrompt, this,
            [this](int platform, const QString& code, const QString& url) {
                if (platform != platform_) return;
                QString text = tr("Autoriza en <a href=\"%1\">%1</a>").arg(url.toHtmlEscaped());
                if (!code.isEmpty()) text += tr(" con el código <b>%1</b>").arg(code.toHtmlEscaped());
                lblStatus_->setTextFormat(Qt::RichText);
                lblStatus_->setOpenExternalLinks(true);
                lblStatus_->setText(text);
            });
    connect(&controller_, &IntegrationController::channelReady, this,
            [this](int platform, const QString& title, const QString& categoryId,
                   const QString& categoryName, const QStringList& tags, bool tagsKnown) {
                if (platform == platform_)
                    loadChannel(title, categoryId, categoryName, tags, tagsKnown);
            });
    connect(&controller_, &IntegrationController::channelFailed, this,
            [this](int platform, const QString& message) {
                if (platform == platform_) setStatus(message, true);
            });
    connect(&controller_, &IntegrationController::publishFinished, this,
            [this](int platform, bool ok, const QString& message, const QString&,
                   const QString&, const QString&, const QStringList&) {
                if (platform != platform_) return;
                setBusy(false);
                if (ok) {
                    setStatus(tr("Publicado correctamente."), false);
                    lblResult_->setText(tr("Última publicación correcta."));
                    lastError_.clear();
                    emitTarget({}, true);
                } else {
                    lastError_ = message;
                    setStatus(message, true);
                    lblResult_->setText(message);
                    emitTarget(message, false);
                }
            });
}

void ProviderPanel::reloadConfig() {
    caps_ = controller_.capabilities(platform_);
    connected_ = false;
    connecting_ = false;
    channelLoaded_ = false;
    updateLimits();
    updateAuth(static_cast<int>(sp::integrations::AuthState::Disconnected), {},
               controller_.configurationProblem(platform_));
}

void ProviderPanel::updateLimits() {
    const QString tagsText = caps_.writeTags
                                 ? tr("Etiquetas: %1 máx., %2 caracteres").arg(caps_.maxTags).arg(caps_.maxTagLen)
                                 : tr("Etiquetas: no soportadas por esta plataforma");
    if (caps_.maxTitleLen)
        lblTitleCount_->setText(tr("Máx. %1 caracteres").arg(caps_.maxTitleLen));
    lblTagCount_->setText(tagsText + tr("   (0/%1)").arg(caps_.maxTags));
}

void ProviderPanel::updateCounts() {
    const int len = edTitle_->text().toUcs4().size();
    if (caps_.maxTitleLen) {
        const bool over = static_cast<std::size_t>(len) > caps_.maxTitleLen;
        lblTitleCount_->setText(tr("%1/%2%3").arg(len).arg(caps_.maxTitleLen)
                                    .arg(over ? tr("  ¡DEMASIADO LARGO!") : QString()));
        lblTitleCount_->setStyleSheet(over ? QStringLiteral("color:#d32f2f;") : QString());
    }
    if (caps_.writeTags) {
        lblTagCount_->setText(tr("%1/%2 etiquetas").arg(tags_.size()).arg(caps_.maxTags));
        lblTagCount_->setStyleSheet(static_cast<std::size_t>(tags_.size()) > caps_.maxTags
                                        ? QStringLiteral("color:#d32f2f;")
                                        : QString());
    }
}

void ProviderPanel::updateAuth(int state, const QString& display, const QString& message) {
    const auto st = static_cast<sp::integrations::AuthState>(state);
    connected_ = st == sp::integrations::AuthState::Connected;
    connecting_ = st == sp::integrations::AuthState::Connecting;
    btnAuth_->setText(connected_ ? tr("Desconectar")
                                 : st == sp::integrations::AuthState::Connecting ? tr("Cancelar")
                                                                                 : tr("Conectar"));
    if (st == sp::integrations::AuthState::Connected) {
        lblStatus_->setTextFormat(Qt::PlainText);
        setStatus(display.isEmpty() ? tr("Conectado.") : tr("Conectado como %1").arg(display), false);
        if (categories_->rowCount() == 0) categories_->refresh();
    } else if (st == sp::integrations::AuthState::Connecting) {
        setStatus(tr("Esperando autorización..."), false);
    } else if (st == sp::integrations::AuthState::Error) {
        setStatus(message.isEmpty() ? tr("Error de conexión.") : message, true);
    } else {
        setStatus(message.isEmpty() ? tr("No conectado.") : message, false);
    }

    const bool canEdit = connected_ && !busy_;
    edTitle_->setEnabled(canEdit);
    chkTitle_->setEnabled(canEdit);
    chkCategory_->setEnabled(canEdit);
    chkTags_->setEnabled(canEdit && caps_.writeTags);
    btnPlanTitle_->setEnabled(canEdit);
    edSearch_->setEnabled(canEdit);
    listCategories_->setEnabled(canEdit);
    edTag_->setEnabled(canEdit && caps_.writeTags);
    btnAddTag_->setEnabled(canEdit && caps_.writeTags);
    btnRemoveTag_->setEnabled(canEdit && caps_.writeTags);
    listTags_->setEnabled(canEdit && caps_.writeTags);
    btnPublish_->setEnabled(canEdit);
}

void ProviderPanel::setStatus(const QString& text, bool error) {
    lblStatus_->setTextFormat(Qt::PlainText);
    lblStatus_->setText(text);
    lblStatus_->setStyleSheet(error ? QStringLiteral("color:#d32f2f;") : QString());
}

void ProviderPanel::updateCategoryLabel() {
    lblCategory_->setText(categoryId_.isEmpty()
                              ? tr("Categoría: (sin seleccionar)")
                              : tr("Categoría: %1  [%2]").arg(categoryName_, categoryId_));
}

void ProviderPanel::selectCategory(int row) {
    const QString id = categories_->idAt(row), name = categories_->nameAt(row);
    if (id.isEmpty()) return;
    categoryId_ = id;
    categoryName_ = name;
    updateCategoryLabel();
    emitTarget({}, false);
}

void ProviderPanel::loadChannel(const QString& title, const QString& categoryId,
                                const QString& categoryName, const QStringList& tags,
                                bool tagsKnown) {
    loading_ = true;
    if (!title.isEmpty()) {
        edTitle_->setText(title);
        title_ = title;
        dirtyTitle_ = false;
    }
    categoryId_ = categoryId;
    categoryName_ = categoryName;
    // Si la API no incluye el campo de etiquetas, se conservan las guardadas
    // localmente para no borrarlas al publicar.
    if (tagsKnown) {
        tags_ = tags;
        channelLoaded_ = true;
    }
    listTags_->clear();
    for (const auto& t : tags_) listTags_->addItem(t);
    if (categories_->rowCount() == 0) categories_->refresh();
    loading_ = false;
    updateCategoryLabel();
    updateCounts();
    setStatus(tr("Datos oficiales cargados desde la plataforma."), false);
}

void ProviderPanel::addTag() {
    QString tag = edTag_->text().trimmed();
    if (tag.isEmpty()) return;
    if (tag.contains(QLatin1Char(' '))) {
        setStatus(tr("Las etiquetas no pueden contener espacios."), true);
        return;
    }
    if (static_cast<std::size_t>(tag.toUcs4().size()) > caps_.maxTagLen) {
        setStatus(tr("La etiqueta supera %1 caracteres.").arg(caps_.maxTagLen), true);
        return;
    }
    if (static_cast<std::size_t>(tags_.size()) >= caps_.maxTags) {
        setStatus(tr("No se pueden añadir más de %1 etiquetas.").arg(caps_.maxTags), true);
        return;
    }
    for (const QString& existing : tags_) {
        if (existing.compare(tag, Qt::CaseInsensitive) == 0) {
            setStatus(tr("Esa etiqueta ya existe."), true);
            return;
        }
    }
    tags_ << tag;
    channelLoaded_ = true;
    listTags_->addItem(tag);
    edTag_->clear();
    updateCounts();
    emitTarget({}, false);
}

void ProviderPanel::removeTag() {
    const int row = listTags_->currentRow();
    if (row < 0 || row >= tags_.size()) return;
    tags_.removeAt(row);
    channelLoaded_ = true;
    delete listTags_->takeItem(row);
    updateCounts();
    emitTarget({}, false);
}

void ProviderPanel::setBusy(bool busy) {
    busy_ = busy;
    btnPublish_->setText(busy ? tr("Publicando...") : tr("Publicar"));
    updateAuth(static_cast<int>(connected_ ? sp::integrations::AuthState::Connected
                                           : sp::integrations::AuthState::Disconnected),
               {}, {});
}

void ProviderPanel::publish() {
    if (!connected_ || busy_) return;
    const QString title = edTitle_->text().trimmed();
    if (chkTitle_->isChecked() && title.isEmpty()) {
        setStatus(tr("Escribe un título antes de publicar."), true);
        return;
    }
    if (chkTitle_->isChecked() && caps_.maxTitleLen > 0 &&
        static_cast<std::size_t>(title.toUcs4().size()) > caps_.maxTitleLen) {
        setStatus(tr("El título supera el límite de %1 caracteres.").arg(caps_.maxTitleLen), true);
        return;
    }
    if (chkCategory_->isChecked() && categoryId_.isEmpty()) {
        setStatus(tr("Elige una categoría antes de publicar."), true);
        return;
    }
    setBusy(true);
    lblResult_->clear();
    const bool applyTags = chkTags_->isChecked() && caps_.writeTags && channelLoaded_;
    controller_.publish(platform_, title, categoryId_, categoryName_, tags_, chkTitle_->isChecked(),
                        chkCategory_->isChecked(), applyTags);
}

void ProviderPanel::emitTarget(const QString& lastError, bool published) {
    if (streamId_ <= 0) return;
    emit targetUpdated(platform_, streamId_, edTitle_->text(), categoryId_, categoryName_, tags_,
                       lastError, published);
}

void ProviderPanel::setContext(int streamId, const QString& planTitle, const sp::StreamTarget* target) {
    loading_ = true;
    streamId_ = streamId;
    planTitle_ = planTitle;
    dirtyTitle_ = false;
    title_.clear();
    categoryId_.clear();
    categoryName_.clear();
    tags_.clear();
    lastError_.clear();
    channelLoaded_ = false;
    lblResult_->clear();

    if (target) {
        title_ = qs(target->title);
        categoryId_ = qs(target->categoryId);
        categoryName_ = qs(target->categoryName);
        tags_ = qsl(target->tags);
        lastError_ = qs(target->lastError);
        channelLoaded_ = !target->tags.empty();
    }
    if (title_.isEmpty()) title_ = planTitle_;
    dirtyTitle_ = target && !target->title.empty() && qs(target->title) != planTitle;
    edTitle_->setText(title_);
    listTags_->clear();
    for (const auto& t : tags_) listTags_->addItem(t);
    updateCategoryLabel();
    if (!lastError_.isEmpty()) lblResult_->setText(lastError_);
    loading_ = false;
    updateCounts();
}

void ProviderPanel::setPlanTitle(const QString& title) {
    planTitle_ = title;
    if (!dirtyTitle_) {
        loading_ = true;
        edTitle_->setText(title);
        loading_ = false;
        updateCounts();
    }
}

// ---------------------------------------------------------------------------------------
// PublishPanel
// ---------------------------------------------------------------------------------------

PublishPanel::PublishPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    auto* notifHead = new QHBoxLayout;
    notifHead->addWidget(new QLabel(tr("Notificación a la comunidad (solo local; las APIs no permiten publicarla)")));
    auto* btnCredentials = new QPushButton(tr("Credenciales..."));
    notifHead->addWidget(btnCredentials);
    auto* btnCopy = new QPushButton(tr("Copiar"));
    notifHead->addWidget(btnCopy);
    v->addLayout(notifHead);
    lblNotif_ = new QLabel;
    lblNotif_->setWordWrap(true);
    lblNotif_->setFrameShape(QFrame::StyledPanel);
    lblNotif_->setMargin(4);
    lblNotif_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(lblNotif_);
    connect(btnCopy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(lblNotif_->text()); });
    connect(btnCredentials, &QPushButton::clicked, this, [this] {
        CredentialsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) reloadCredentials();
    });

    auto* tabs = new QTabWidget;
    twitch_ = new ProviderPanel(controller_, static_cast<int>(sp::integrations::Platform::Twitch));
    kick_ = new ProviderPanel(controller_, static_cast<int>(sp::integrations::Platform::Kick));
    struct Tab {
        ProviderPanel* panel;
        const char* name;
    };
    for (const Tab& tab : {Tab{twitch_, "Twitch"}, Tab{kick_, "Kick"}}) {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(tab.panel);
        tabs->addTab(scroll, QString::fromLatin1(tab.name));
        connect(tab.panel, &ProviderPanel::targetUpdated, this, &PublishPanel::targetUpdated);
    }
    v->addWidget(tabs, 1);

    lblSummary_ = new QLabel;
    lblSummary_->setWordWrap(true);
    v->addWidget(lblSummary_);

    controller_.restoreAll();
}

void PublishPanel::reloadCredentials() {
    twitch_->reloadConfig();
    kick_->reloadConfig();
    controller_.reload();
}

void PublishPanel::setContext(int streamId, const sp::Preview& preview, const sp::Stream* stream) {
    streamId_ = streamId;
    preview_ = preview;
    lblNotif_->setText(qs(preview.notif));
    twitch_->setContext(streamId, qs(preview.title),
                        stream ? stream->findTarget("twitch") : nullptr);
    kick_->setContext(streamId, qs(preview.title),
                      stream ? stream->findTarget("kick") : nullptr);
    lblSummary_->setText(tr("Título del plan: %1 caracteres · Twitch máx. 140 · Kick máx. 100")
                             .arg(preview.titleLen));
}

void PublishPanel::setPlanTitle(const sp::Preview& preview) {
    preview_ = preview;
    lblNotif_->setText(qs(preview.notif));
    twitch_->setPlanTitle(qs(preview.title));
    kick_->setPlanTitle(qs(preview.title));
    lblSummary_->setText(tr("Título del plan: %1 caracteres · Twitch máx. 140 · Kick máx. 100")
                             .arg(preview.titleLen));
}
