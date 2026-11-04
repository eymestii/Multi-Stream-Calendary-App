// Panel de publicación en Twitch/Kick. Es una vista: toda la lógica está en
// core/integrations y en IntegrationController.
#pragma once
#include <QAbstractListModel>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "core/planner.h"
#include "integration_controller.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTimer;

class CategoryModel : public QAbstractListModel {
    Q_OBJECT
public:
    CategoryModel(IntegrationController& controller, int platform, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool canFetchMore(const QModelIndex& parent = QModelIndex()) const override;
    void fetchMore(const QModelIndex& parent = QModelIndex()) override;

    void scheduleSearch(const QString& text);
    void refresh();
    QString idAt(int row) const;
    QString nameAt(int row) const;

signals:
    void failed(const QString& message);

private:
    struct Item {
        QString id;
        QString name;
    };
    void startRequest(const QString& query, const QString& cursor, bool append);

    IntegrationController& controller_;
    int platform_;
    QVector<Item> items_;
    QString query_;
    QString cursor_;
    bool hasMore_ = false;
    bool pending_ = false;
    bool append_ = false;
    int requestId_ = 0;
    int pendingId_ = 0;
    QTimer* debounce_;
};

class ProviderPanel : public QWidget {
    Q_OBJECT
public:
    ProviderPanel(IntegrationController& controller, int platform, QWidget* parent = nullptr);

    int platform() const { return platform_; }
    void setContext(int streamId, const QString& planTitle, const sp::StreamTarget* target);
    void setPlanTitle(const QString& title);
    // Relee capacidades y estado de configuración tras cambiar credenciales.
    void reloadConfig();

signals:
    void targetUpdated(int platform, int streamId, const QString& title, const QString& categoryId,
                       const QString& categoryName, const QStringList& tags,
                       const QString& lastError, bool published);

private:
    void buildUi();
    void connectSignals();
    void updateAuth(int state, const QString& display, const QString& message);
    void updateLimits();
    void updateCounts();
    void loadChannel(const QString& title, const QString& categoryId, const QString& categoryName,
                     const QStringList& tags, bool tagsKnown);
    void selectCategory(int row);
    void addTag();
    void removeTag();
    void publish();
    void updateCategoryLabel();
    void emitTarget(const QString& lastError, bool published);
    QString categoryLabel() const;
    void setBusy(bool busy);
    void setStatus(const QString& text, bool error = false);

    IntegrationController& controller_;
    int platform_;
    int streamId_ = 0;
    QString planTitle_;
    QString title_;
    QString categoryId_;
    QString categoryName_;
    QStringList tags_;
    QString lastError_;
    bool loading_ = true;
    bool dirtyTitle_ = false;
    bool busy_ = false;
    bool connected_ = false;
    bool connecting_ = false;
    bool channelLoaded_ = false;
    sp::integrations::Capabilities caps_;

    QLabel* lblStatus_{};
    QPushButton* btnAuth_{};
    QPushButton* btnRefresh_{};
    QLineEdit* edTitle_{};
    QLabel* lblTitleCount_{};
    QCheckBox* chkTitle_{};
    QCheckBox* chkCategory_{};
    QCheckBox* chkTags_{};
    QPushButton* btnPlanTitle_{};
    QLineEdit* edSearch_{};
    QListView* listCategories_{};
    QLabel* lblCategory_{};
    CategoryModel* categories_{};
    QLineEdit* edTag_{};
    QPushButton* btnAddTag_{};
    QListWidget* listTags_{};
    QPushButton* btnRemoveTag_{};
    QLabel* lblTagCount_{};
    QPushButton* btnPublish_{};
    QLabel* lblResult_{};
};

class PublishPanel : public QWidget {
    Q_OBJECT
public:
    explicit PublishPanel(QWidget* parent = nullptr);

    // streamId + preview + stream seleccionado (con su estado por plataforma).
    void setContext(int streamId, const sp::Preview& preview, const sp::Stream* stream);
    // Actualiza solo la notificación y el título "del plan" (sin pisar ediciones).
    void setPlanTitle(const sp::Preview& preview);

signals:
    void targetUpdated(int platform, int streamId, const QString& title, const QString& categoryId,
                       const QString& categoryName, const QStringList& tags,
                       const QString& lastError, bool published);

private:
    void reloadCredentials();

    IntegrationController controller_;
    ProviderPanel* twitch_{};
    ProviderPanel* kick_{};
    QLabel* lblNotif_{};
    QLabel* lblSummary_{};
    int streamId_ = 0;
    sp::Preview preview_;
};
