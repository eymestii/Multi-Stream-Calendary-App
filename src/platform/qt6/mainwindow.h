// Ventana principal Qt6. Es una VISTA: lee del sp::Planner y le envía órdenes.
// No contiene reglas de negocio (límites, ordenado, persistencia): eso vive en src/core.
#pragma once
#include <QMainWindow>
#include <array>
#include <string>
#include <vector>

#include "core/planner.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(sp::Planner& planner, QWidget* parent = nullptr);

private:
    // construcción
    void buildUi();
    QWidget* buildLeftPanel();
    QWidget* buildRightPanel();
    void nameWidgets();  // objectName: tests y hojas de estilo
    void connectSignals();

    // estado de la vista -> datos para el core
    sp::Draft draft() const;
    sp::Stream referenceStream() const;
    std::vector<sp::TagValue> enteredTags() const;

    // refrescos (leen el modelo y actualizan widgets)
    void refreshSeriesList();
    void refreshKanban();
    void updateStats();
    void updatePreview();
    void syncTagFields(bool force = false);
    void loadFields();
    void selectSeries(int id, bool repopulateList = true);

    // acciones del usuario
    void actNewSeries();
    void actSaveSeries();
    void actToggleActive();
    void actDeleteSeries();
    void actAddStream();
    void actMoveStream(int dir);
    void actDeleteStream();
    void onKanbanSelection(int column);

    // ayudas
    void warn(const QString& msg);
    void reportError(const sp::Outcome& o);
    static void setError(QWidget* w, bool on);

    sp::Planner& planner_;
    int curSeries_ = 0;
    int selStream_ = 0;
    bool loading_ = true;  // evita bucles mientras se rellenan campos por código
    bool titleOver_ = false, notifOver_ = false, tagsOver_ = false;
    std::vector<std::string> tags_;  // etiquetas personalizadas detectadas en las plantillas
    QString tagsInfo_;

    // widgets
    QListWidget* lbSeries_{};
    QPushButton *btnNew_{}, *btnToggle_{}, *btnDelS_{};
    QLineEdit *edName_{}, *edTitle_{}, *edNotif_{};
    QLabel *stTags_{}, *stTitlePrev_{}, *stTitleCount_{}, *stNotifPrev_{}, *stNotifCount_{}, *stStats_{};
    QPushButton *btnSave_{}, *btnCopyT_{}, *btnCopyN_{};
    std::array<QListWidget*, 3> lbCol_{};
    std::array<QLabel*, sp::kMaxTags> tagLbl_{};
    std::array<QLineEdit*, sp::kMaxTags> tagEd_{};
    QLineEdit *edDay_{}, *edDate_{};
    QPushButton *btnAdd_{}, *btnLeft_{}, *btnRight_{}, *btnDelT_{};
};
