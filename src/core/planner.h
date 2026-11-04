// Lógica de aplicación independiente de la interfaz: validaciones, vista previa,
// ordenado del kanban y operaciones con persistencia. Toda UI (Win32, Qt6...)
// debe limitarse a pintar estos datos y a llamar a estos métodos.
// Portable: NO incluir windows.h ni Qt aquí.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/paths.h"
#include "core/store.h"
#include "core/text.h"
#include "core/types.h"

namespace sp {

std::string todayIso();            // YYYY-MM-DD (hora local)
int parseDay(std::string_view s);  // 0 si no es un entero de 1..5 dígitos

// ---- Vista previa ----------------------------------------------------------------

struct Draft {  // lo que hay escrito ahora mismo en el editor de la serie
    std::string name, titleTemplate, notifTemplate;
};

struct Preview {
    std::string title, notif;
    std::size_t titleLen = 0, notifLen = 0;  // en caracteres (code points)
    bool titleOver = false, notifOver = false;
    std::vector<std::string> tags;  // etiquetas personalizadas (máx. kMaxTags)
    std::size_t tagTotal = 0;       // las detectadas, antes de recortar
    bool tagsOver = false;          // tagTotal > kMaxTags
};

Preview makePreview(const Draft& d, const Stream& reference);

// Stream "de ejemplo" para previsualizar sin stream seleccionado: los valores vacíos
// se muestran como <nombre>. `values` va en el mismo orden que `names`.
Stream makeSample(int day, std::string date, const std::vector<std::string>& names,
                  const std::vector<std::string>& values);

// ---- Resultados de operaciones --------------------------------------------------------

enum class Err {
    None,
    NoSeries,
    NoStream,
    InvalidDay,
    TitleTooLong,
    NotifTooLong,
    TooManyTags,
    StorageFailed
};

struct Outcome {
    Err err = Err::None;
    int id = 0;                                // id creado, si aplica
    std::size_t titleLen = 0, notifLen = 0;    // longitudes calculadas (para el mensaje)
    bool ok() const { return err == Err::None; }
};

// ---- Planner ---------------------------------------------------------------------

class Planner {
public:
    explicit Planner(DataPaths paths) : paths_(std::move(paths)) {}

    // false si no se pudo abrir la base de datos: la UI debe cerrar sin guardar nada.
    bool open() { return store_.load(paths_.db, paths_.legacy); }

    // Consultas
    const std::vector<Series>& series() const { return store_.series; }
    const Series* findSeries(int id) const;
    const Stream* findStream(int id) const;
    std::vector<const Stream*> streamsOf(int seriesId) const;  // por día y luego por creación
    int count(int seriesId, Status st) const { return store_.count(seriesId, st); }
    int latestDay(int seriesId) const { return store_.latestDay(seriesId); }
    int firstSeriesId() const { return store_.series.empty() ? 0 : store_.series.front().id; }

    // Órdenes (todas persisten al terminar)
    Outcome createSeries(const std::string& name);
    // `reference` es el stream con el que la UI muestra la vista previa; se valida contra él.
    Outcome saveSeries(int id, const Draft& d, const Stream& reference);
    Outcome toggleActive(int id);
    Outcome deleteSeries(int id);
    // Se valida contra las plantillas GUARDADAS de la serie. date vacía => hoy.
    Outcome addStream(int seriesId, int day, std::string date, std::vector<TagValue> tags);
    Outcome moveStream(int streamId, int dir);  // -1 / +1 entre Planeado, En vivo y Realizado
    Outcome deleteStream(int streamId);

    // Estado por plataforma (borrador + último resultado de publicación).
    const StreamTarget* target(int streamId, std::string_view platform) const;
    Outcome saveTarget(int streamId, const StreamTarget& target);

private:
    Outcome persist(Outcome o = {});

    Store store_;
    DataPaths paths_;
};

}  // namespace sp
