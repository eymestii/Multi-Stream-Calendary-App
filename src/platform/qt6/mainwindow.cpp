#include "mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

#include "integration/publishpanel.h"
#include "qtutil.h"

namespace {

constexpr int kIdRole = Qt::UserRole;  // id (serie o stream) guardado en cada item
constexpr int kTagCols = 3;            // columnas del formulario de etiquetas

QLabel* plainLabel(const QString& text = {}) {
    auto* l = new QLabel(text);
    l->setTextFormat(Qt::PlainText);  // el texto del usuario puede contener <algo>: nunca como HTML
    return l;
}

QLabel* previewLabel() {
    auto* l = plainLabel();
    l->setWordWrap(true);
    l->setFrameShape(QFrame::StyledPanel);
    l->setMargin(4);
    l->setMinimumHeight(l->fontMetrics().lineSpacing() * 2 + 12);
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

}  // namespace

MainWindow::MainWindow(sp::Planner& planner, QWidget* parent) : QMainWindow(parent), planner_(planner) {
    setWindowTitle(QStringLiteral("Stream Planner"));
    setMinimumSize(960, 720);
    resize(1180, 800);

    buildUi();
    nameWidgets();
    connectSignals();

    curSeries_ = planner_.firstSeriesId();
    refreshSeriesList();
    loadFields();
}

// ---------------------------------------------------------------------------------------
// Construcción
// ---------------------------------------------------------------------------------------

void MainWindow::buildUi() {
    auto* central = new QWidget;
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);
    root->addWidget(buildLeftPanel());
    root->addWidget(buildRightPanel(), 1);
    setCentralWidget(central);
}

QWidget* MainWindow::buildLeftPanel() {
    auto* w = new QWidget;
    w->setFixedWidth(250);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    v->addWidget(plainLabel(tr("Series")));
    lbSeries_ = new QListWidget;
    v->addWidget(lbSeries_, 1);
    btnNew_ = new QPushButton(tr("Nueva serie"));
    btnToggle_ = new QPushButton(tr("Activa / Finalizada"));
    btnDelS_ = new QPushButton(tr("Borrar serie"));
    v->addWidget(btnNew_);
    v->addWidget(btnToggle_);
    v->addWidget(btnDelS_);
    return w;
}

QWidget* MainWindow::buildRightPanel() {
    auto* tabs = new QTabWidget;
    tabs->addTab(buildPlanningPanel(), tr("Planificación"));
    publish_ = new PublishPanel;
    publish_->setObjectName(QStringLiteral("publishPanel"));
    tabs->addTab(publish_, tr("Plataformas"));
    connect(publish_, &PublishPanel::targetUpdated, this,
            [this](int platform, int streamId, const QString& title, const QString& categoryId,
                   const QString& categoryName, const QStringList& tags, const QString& lastError,
                   bool published) {
                if (streamId <= 0) return;
                sp::StreamTarget t;
                t.platform = platform == 1 ? "kick" : "twitch";
                t.title = ss(title);
                t.categoryId = ss(categoryId);
                t.categoryName = ss(categoryName);
                for (const auto& tag : tags) t.tags.push_back(ss(tag));
                t.lastError = ss(lastError);
                if (const sp::StreamTarget* old = planner_.target(streamId, t.platform))
                    t.lastPublishedAt = old->lastPublishedAt;
                if (published)
                    t.lastPublishedAt =
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                planner_.saveTarget(streamId, t);
            });
    return tabs;
}

QWidget* MainWindow::buildPlanningPanel() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    // Nombre
    v->addWidget(plainLabel(tr("Nombre de la serie")));
    edName_ = new QLineEdit;
    v->addWidget(edName_);

    // Etiquetas detectadas
    v->addWidget(plainLabel(tr("Etiquetas   (fijas: {series} {day} {date}; las demás se crean "
                               "escribiéndolas entre llaves en una plantilla)")));
    stTags_ = plainLabel();
    v->addWidget(stTags_);

    // Plantilla del título
    v->addWidget(plainLabel(tr("Plantilla del título   (ej: [{series}] Día {day} | {topic})")));
    edTitle_ = new QLineEdit;
    v->addWidget(edTitle_);
    stTitlePrev_ = previewLabel();
    v->addWidget(stTitlePrev_);
    stTitleCount_ = plainLabel();
    v->addWidget(stTitleCount_);

    // Plantilla de la notificación
    v->addWidget(plainLabel(tr("Plantilla de la notificación a la comunidad")));
    edNotif_ = new QLineEdit;
    v->addWidget(edNotif_);
    stNotifPrev_ = previewLabel();
    v->addWidget(stNotifPrev_);
    stNotifCount_ = plainLabel();
    v->addWidget(stNotifCount_);

    // Botones de serie
    auto* row = new QHBoxLayout;
    btnSave_ = new QPushButton(tr("Guardar serie"));
    btnCopyT_ = new QPushButton(tr("Copiar título"));
    btnCopyN_ = new QPushButton(tr("Copiar notificación"));
    row->addWidget(btnSave_);
    row->addWidget(btnCopyT_);
    row->addWidget(btnCopyN_);
    row->addStretch(1);
    v->addLayout(row);
    stStats_ = plainLabel();
    v->addWidget(stStats_);

    // Kanban: Planeado / En vivo / Realizado
    auto* kanban = new QHBoxLayout;
    const QString heads[3] = {tr("Planeado"), tr("En vivo"), tr("Realizado")};
    for (int c = 0; c < 3; ++c) {
        auto* col = new QVBoxLayout;
        col->addWidget(plainLabel(heads[c]));
        lbCol_[c] = new QListWidget;
        lbCol_[c]->setSelectionMode(QAbstractItemView::SingleSelection);
        col->addWidget(lbCol_[c], 1);
        kanban->addLayout(col, 1);
    }
    v->addLayout(kanban, 1);

    // Campos de las etiquetas: se crean todos (ocultos) y se muestran solo los necesarios
    auto* tagGrid = new QGridLayout;
    for (std::size_t i = 0; i < sp::kMaxTags; ++i) {
        tagLbl_[i] = plainLabel();
        tagEd_[i] = new QLineEdit;
        tagEd_[i]->setMaxLength(static_cast<int>(sp::kTwitchLimit));
        const int r = static_cast<int>(i) / kTagCols * 2, c = static_cast<int>(i) % kTagCols;
        tagGrid->addWidget(tagLbl_[i], r, c);
        tagGrid->addWidget(tagEd_[i], r + 1, c);
        tagLbl_[i]->hide();
        tagEd_[i]->hide();
    }
    for (int c = 0; c < kTagCols; ++c) tagGrid->setColumnStretch(c, 1);
    v->addLayout(tagGrid);

    // Fila inferior: día, fecha y botones de stream
    auto* bottom = new QHBoxLayout;
    auto* dayBox = new QVBoxLayout;
    dayBox->addWidget(plainLabel(tr("Día")));
    edDay_ = new QLineEdit(QStringLiteral("1"));
    edDay_->setFixedWidth(60);
    edDay_->setMaxLength(5);
    edDay_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,5}")), edDay_));
    dayBox->addWidget(edDay_);
    auto* dateBox = new QVBoxLayout;
    dateBox->addWidget(plainLabel(tr("Fecha")));
    edDate_ = new QLineEdit(qs(sp::todayIso()));
    edDate_->setFixedWidth(120);
    edDate_->setPlaceholderText(tr("AAAA-MM-DD"));
    dateBox->addWidget(edDate_);
    bottom->addLayout(dayBox);
    bottom->addLayout(dateBox);

    btnAdd_ = new QPushButton(tr("Añadir stream"));
    btnLeft_ = new QPushButton(QStringLiteral("<"));
    btnRight_ = new QPushButton(QStringLiteral(">"));
    btnDelT_ = new QPushButton(tr("Borrar"));
    btnLeft_->setFixedWidth(40);
    btnRight_->setFixedWidth(40);
    auto* btns = new QHBoxLayout;
    btns->addWidget(btnAdd_);
    btns->addWidget(btnLeft_);
    btns->addWidget(btnRight_);
    btns->addWidget(btnDelT_);
    bottom->addLayout(btns);
    bottom->addStretch(1);
    // Alinea los botones con la línea de los campos (debajo de las etiquetas)
    bottom->setAlignment(btns, Qt::AlignBottom);
    v->addLayout(bottom);
    return w;
}

void MainWindow::nameWidgets() {
    const std::pair<QWidget*, const char*> names[] = {
        {lbSeries_, "lbSeries"}, {btnNew_, "btnNew"}, {btnToggle_, "btnToggle"}, {btnDelS_, "btnDeleteSeries"},
        {edName_, "edName"}, {edTitle_, "edTitle"}, {edNotif_, "edNotif"}, {stTags_, "stTags"},
        {stTitlePrev_, "stTitlePreview"}, {stTitleCount_, "stTitleCount"},
        {stNotifPrev_, "stNotifPreview"}, {stNotifCount_, "stNotifCount"}, {stStats_, "stStats"},
        {btnSave_, "btnSave"}, {btnCopyT_, "btnCopyTitle"}, {btnCopyN_, "btnCopyNotif"},
        {edDay_, "edDay"}, {edDate_, "edDate"}, {btnAdd_, "btnAdd"}, {btnLeft_, "btnLeft"},
        {btnRight_, "btnRight"}, {btnDelT_, "btnDeleteStream"},
        {lbCol_[0], "lbPlanned"}, {lbCol_[1], "lbLive"}, {lbCol_[2], "lbDone"}};
    for (const auto& [w, n] : names) w->setObjectName(QString::fromLatin1(n));
    for (std::size_t i = 0; i < sp::kMaxTags; ++i) {
        tagLbl_[i]->setObjectName(QStringLiteral("tagLabel%1").arg(i));
        tagEd_[i]->setObjectName(QStringLiteral("tagEdit%1").arg(i));
    }
}

void MainWindow::connectSignals() {
    // Serie
    connect(lbSeries_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (it && !loading_) selectSeries(it->data(kIdRole).toInt(), /*repopulateList=*/false);
    });
    connect(btnNew_, &QPushButton::clicked, this, &MainWindow::actNewSeries);
    connect(btnToggle_, &QPushButton::clicked, this, &MainWindow::actToggleActive);
    connect(btnDelS_, &QPushButton::clicked, this, &MainWindow::actDeleteSeries);
    connect(btnSave_, &QPushButton::clicked, this, &MainWindow::actSaveSeries);
    new QShortcut(QKeySequence::Save, this, [this] { actSaveSeries(); });

    connect(btnCopyT_, &QPushButton::clicked, this, [] {});  // se reasigna abajo con el texto real
    btnCopyT_->disconnect();
    connect(btnCopyT_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(stTitlePrev_->text()); });
    connect(btnCopyN_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(stNotifPrev_->text()); });

    // Edición de plantillas: las etiquetas se crean al escribir {nombre}
    connect(edName_, &QLineEdit::textChanged, this, [this] { if (!loading_) updatePreview(); });
    for (QLineEdit* e : {edTitle_, edNotif_})
        connect(e, &QLineEdit::textChanged, this, [this] {
            if (loading_) return;
            syncTagFields();
            updatePreview();
        });
    for (QLineEdit* e : {edDay_, edDate_})
        connect(e, &QLineEdit::textChanged, this, [this] { if (!loading_) updatePreview(); });
    for (QLineEdit* e : tagEd_)
        connect(e, &QLineEdit::textChanged, this, [this] { if (!loading_) updatePreview(); });

    // Streams
    for (int c = 0; c < 3; ++c)
        connect(lbCol_[c], &QListWidget::itemSelectionChanged, this, [this, c] { onKanbanSelection(c); });
    connect(btnAdd_, &QPushButton::clicked, this, &MainWindow::actAddStream);
    connect(edDay_, &QLineEdit::returnPressed, this, &MainWindow::actAddStream);
    connect(edDate_, &QLineEdit::returnPressed, this, &MainWindow::actAddStream);
    for (QLineEdit* e : tagEd_) connect(e, &QLineEdit::returnPressed, this, &MainWindow::actAddStream);
    connect(btnLeft_, &QPushButton::clicked, this, [this] { actMoveStream(-1); });
    connect(btnRight_, &QPushButton::clicked, this, [this] { actMoveStream(+1); });
    connect(btnDelT_, &QPushButton::clicked, this, &MainWindow::actDeleteStream);
}

// ---------------------------------------------------------------------------------------
// Vista -> core
// ---------------------------------------------------------------------------------------

sp::Draft MainWindow::draft() const {
    return {ss(edName_->text()), ss(edTitle_->text()), ss(edNotif_->text())};
}

std::vector<sp::TagValue> MainWindow::enteredTags() const {
    std::vector<sp::TagValue> out;
    for (std::size_t i = 0; i < tags_.size() && i < sp::kMaxTags; ++i) {
        std::string v = ss(tagEd_[i]->text());
        if (!v.empty()) out.push_back({tags_[i], std::move(v)});
    }
    return out;
}

// Stream con el que se calcula la vista previa: el seleccionado, o uno de ejemplo
// construido con lo que haya escrito en el formulario.
sp::Stream MainWindow::referenceStream() const {
    if (const sp::Stream* st = planner_.findStream(selStream_)) return *st;
    int day = sp::parseDay(ss(edDay_->text()));
    if (day <= 0) day = planner_.latestDay(curSeries_);
    std::vector<std::string> values;
    for (std::size_t i = 0; i < tags_.size() && i < sp::kMaxTags; ++i) values.push_back(ss(tagEd_[i]->text()));
    return sp::makeSample(day, ss(edDate_->text()), tags_, values);
}

// ---------------------------------------------------------------------------------------
// Refrescos
// ---------------------------------------------------------------------------------------

void MainWindow::refreshSeriesList() {
    const QSignalBlocker block(lbSeries_);
    lbSeries_->clear();
    int sel = -1, i = 0;
    for (const sp::Series& s : planner_.series()) {
        auto* it = new QListWidgetItem((s.active ? tr("[activa] ") : tr("[finalizada] ")) + qs(s.name));
        it->setData(kIdRole, s.id);
        lbSeries_->addItem(it);
        if (s.id == curSeries_) sel = i;
        ++i;
    }
    lbSeries_->setCurrentRow(sel);
}

void MainWindow::refreshKanban() {
    for (QListWidget* l : lbCol_) {
        const QSignalBlocker block(l);
        l->clear();
    }
    const sp::Series* s = planner_.findSeries(curSeries_);
    if (!s) return;
    for (const sp::Stream* t : planner_.streamsOf(s->id)) {
        QString txt = tr("Día ") + QString::number(t->number);
        bool first = true;
        for (const auto& tv : t->tags) {
            if (tv.value.empty()) continue;
            txt += first ? QStringLiteral(" - ") : QStringLiteral(" · ");
            first = false;
            txt += qs(tv.value);
        }
        txt += QStringLiteral("  (") + qs(t->date) + QLatin1Char(')');

        QListWidget* col = lbCol_[static_cast<std::size_t>(t->status)];
        auto* it = new QListWidgetItem(txt);
        it->setData(kIdRole, t->id);
        col->addItem(it);
        if (t->id == selStream_) {
            const QSignalBlocker block(col);
            col->setCurrentItem(it);
        }
    }
}

void MainWindow::updateStats() {
    if (!planner_.findSeries(curSeries_)) {
        stStats_->setText(tr("Crea o selecciona una serie."));
        return;
    }
    stStats_->setText(tr("Planeado: %1   |   En vivo: %2   |   Realizado: %3")
                          .arg(planner_.count(curSeries_, sp::Status::Planned))
                          .arg(planner_.count(curSeries_, sp::Status::Live))
                          .arg(planner_.count(curSeries_, sp::Status::Done)));
}

void MainWindow::setError(QWidget* w, bool on) {
    if (w->property("err").toBool() == on) return;  // evita reaplicar estilos en cada tecla
    w->setProperty("err", on);
    w->setStyleSheet(on ? QStringLiteral("color: #d32f2f;") : QString());
}

void MainWindow::updatePreview() {
    const sp::Preview p = sp::makePreview(draft(), referenceStream());
    titleOver_ = p.titleOver;
    notifOver_ = p.notifOver;

    stTitlePrev_->setText(qs(p.title));
    stNotifPrev_->setText(qs(p.notif));
    const qulonglong lim = sp::kTwitchLimit;
    stTitleCount_->setText(tr("Título: %1/%2%3").arg(p.titleLen).arg(lim).arg(p.titleOver ? tr("  ¡DEMASIADO LARGO!") : QString()));
    stNotifCount_->setText(tr("Notificación: %1/%2%3").arg(p.notifLen).arg(lim).arg(p.notifOver ? tr("  ¡DEMASIADA LARGA!") : QString()));
    setError(stTitleCount_, p.titleOver);
    setError(stNotifCount_, p.notifOver);
    if (publish_) publish_->setPlanTitle(p);
}

// Carga en el panel de plataformas el stream seleccionado y su último estado guardado.
void MainWindow::updatePublishContext() {
    if (!publish_) return;
    const sp::Preview p = sp::makePreview(draft(), referenceStream());
    publish_->setContext(selStream_, p, planner_.findStream(selStream_));
}

// Detecta las {etiquetas} de las plantillas (lo escrito ahora mismo) y muestra/oculta los
// campos de valor. Lo ya escrito en un campo se conserva por nombre.
void MainWindow::syncTagFields(bool force) {
    std::vector<std::string> now = sp::extractTags(ss(edTitle_->text()), ss(edNotif_->text()));
    const std::size_t total = now.size();
    tagsOver_ = total > sp::kMaxTags;
    if (tagsOver_) now.resize(sp::kMaxTags);  // la UI solo maneja las primeras 10

    QString info = QStringLiteral("(%1/%2)").arg(total).arg(sp::kMaxTags);
    if (total == 0) info += tr("  ninguna todavía");
    for (const std::string& name : now) info += QStringLiteral("  {") + qs(name) + QLatin1Char('}');
    if (tagsOver_) info += tr("  ¡DEMASIADAS!");
    if (info != tagsInfo_) {
        tagsInfo_ = info;
        stTags_->setText(info);
        setError(stTags_, tagsOver_);
    }

    if (!force && now == tags_) return;

    std::vector<QString> oldVals;
    for (std::size_t i = 0; i < tags_.size(); ++i) oldVals.push_back(tagEd_[i]->text());
    const std::vector<std::string> oldNames = std::move(tags_);
    tags_ = std::move(now);

    const bool prev = loading_;
    loading_ = true;
    for (std::size_t i = 0; i < sp::kMaxTags; ++i) {
        const bool on = i < tags_.size();
        if (on) {
            QString val;
            for (std::size_t j = 0; j < oldNames.size(); ++j)
                if (oldNames[j] == tags_[i]) { val = oldVals[j]; break; }
            tagLbl_[i]->setText(qs(tags_[i]));
            tagEd_[i]->setText(val);
        }
        tagLbl_[i]->setVisible(on);
        tagEd_[i]->setVisible(on);
    }
    loading_ = prev;
}

void MainWindow::loadFields() {
    loading_ = true;
    const sp::Series* s = planner_.findSeries(curSeries_);
    edName_->setText(s ? qs(s->name) : QString());
    edTitle_->setText(s ? qs(s->titleTemplate) : QString());
    edNotif_->setText(s ? qs(s->notifTemplate) : QString());
    edDay_->setText(s ? QString::number(planner_.latestDay(s->id)) : QString());
    for (QLineEdit* e : tagEd_) e->clear();  // valores nuevos en blanco
    tags_.clear();
    loading_ = false;
    syncTagFields(true);
    refreshKanban();
    updatePreview();
    updateStats();
    updatePublishContext();
}

void MainWindow::selectSeries(int id, bool repopulateList) {
    curSeries_ = id;
    selStream_ = 0;
    if (repopulateList) refreshSeriesList();  // no se repuebla desde la propia señal de la lista
    loadFields();
}

// ---------------------------------------------------------------------------------------
// Acciones
// ---------------------------------------------------------------------------------------

void MainWindow::warn(const QString& msg) {
    QMessageBox::warning(this, QStringLiteral("Stream Planner"), msg);
}

void MainWindow::reportError(const sp::Outcome& o) {
    switch (o.err) {
        case sp::Err::None: return;
        case sp::Err::NoSeries: warn(tr("Primero crea una serie.")); break;
        case sp::Err::NoStream: break;
        case sp::Err::InvalidDay: warn(tr("Escribe un día válido (1 o más).")); edDay_->setFocus(); break;
        case sp::Err::TitleTooLong:
        case sp::Err::NotifTooLong:
            warn(tr("El título o la notificación superan el límite de Twitch (%1). Título: %2, notificación: %3. "
                    "Acorta el texto.")
                     .arg(sp::kTwitchLimit).arg(o.titleLen).arg(o.notifLen));
            break;
        case sp::Err::TooManyTags:
            warn(tr("Las plantillas usan más de %1 etiquetas personalizadas. Quita algunas antes de guardar.")
                     .arg(sp::kMaxTags));
            break;
        case sp::Err::StorageFailed:
            warn(tr("No se pudo guardar en la base de datos. Los cambios recientes podrían no haberse conservado."));
            break;
    }
}

void MainWindow::actNewSeries() {
    const sp::Outcome o = planner_.createSeries(ss(tr("Nueva serie")));
    reportError(o);
    if (o.id == 0) return;
    selectSeries(o.id);
    edName_->setFocus();
    edName_->selectAll();
}

void MainWindow::actSaveSeries() {
    if (!planner_.findSeries(curSeries_)) return;
    const sp::Outcome o = planner_.saveSeries(curSeries_, draft(), referenceStream());
    if (!o.ok()) { reportError(o); return; }
    refreshSeriesList();
    updatePreview();
}

void MainWindow::actToggleActive() {
    if (!planner_.findSeries(curSeries_)) return;
    reportError(planner_.toggleActive(curSeries_));
    refreshSeriesList();
    updateStats();
}

void MainWindow::actDeleteSeries() {
    if (!planner_.findSeries(curSeries_)) return;
    if (QMessageBox::question(this, QStringLiteral("Stream Planner"), tr("¿Borrar la serie y todos sus streams?")) !=
        QMessageBox::Yes)
        return;
    reportError(planner_.deleteSeries(curSeries_));
    selectSeries(planner_.firstSeriesId());
}

void MainWindow::actAddStream() {
    const sp::Outcome o =
        planner_.addStream(curSeries_, sp::parseDay(ss(edDay_->text())), ss(edDate_->text()), enteredTags());
    if (!o.ok()) {
        if (o.err == sp::Err::TitleTooLong || o.err == sp::Err::NotifTooLong)
            warn(tr("Con esos datos el título queda en %1 caracteres y la notificación en %2 (máximo %3). "
                    "Acorta los valores.").arg(o.titleLen).arg(o.notifLen).arg(sp::kTwitchLimit));
        else
            reportError(o);
        return;
    }
    selStream_ = o.id;

    loading_ = true;  // limpiar valores sin disparar vistas previas intermedias
    for (std::size_t i = 0; i < tags_.size() && i < sp::kMaxTags; ++i) tagEd_[i]->clear();
    loading_ = false;

    refreshKanban();
    updatePreview();
    updateStats();
    updatePublishContext();
}

void MainWindow::actMoveStream(int dir) {
    if (!planner_.findStream(selStream_)) return;
    reportError(planner_.moveStream(selStream_, dir));
    refreshKanban();
    updateStats();
}

void MainWindow::actDeleteStream() {
    if (!planner_.findStream(selStream_)) return;
    reportError(planner_.deleteStream(selStream_));
    selStream_ = 0;
    refreshKanban();
    updatePreview();
    updateStats();
    updatePublishContext();
}

void MainWindow::onKanbanSelection(int column) {
    const auto items = lbCol_[static_cast<std::size_t>(column)]->selectedItems();
    if (items.isEmpty()) return;
    selStream_ = items.first()->data(kIdRole).toInt();
    for (int k = 0; k < 3; ++k) {
        if (k == column) continue;
        const QSignalBlocker block(lbCol_[static_cast<std::size_t>(k)]);
        lbCol_[static_cast<std::size_t>(k)]->clearSelection();
        lbCol_[static_cast<std::size_t>(k)]->setCurrentItem(nullptr);
    }
    updatePreview();
    updatePublishContext();
}
