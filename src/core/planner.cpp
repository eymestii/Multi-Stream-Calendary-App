#include "core/planner.h"

#include <algorithm>
#include <charconv>
#include <ctime>

namespace sp {

std::string todayIso() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}

int parseDay(std::string_view s) {
    if (s.empty() || s.size() > 5) return 0;
    int v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size() || v < 0) return 0;
    return v;
}

Preview makePreview(const Draft& d, const Stream& reference) {
    Series tmp;  // render() solo usa el nombre de la serie
    tmp.name = d.name;

    Preview p;
    p.title = render(d.titleTemplate, tmp, reference);
    p.notif = render(d.notifTemplate, tmp, reference);
    p.titleLen = utf8Length(p.title);
    p.notifLen = utf8Length(p.notif);
    p.titleOver = p.titleLen > kTwitchLimit;
    p.notifOver = p.notifLen > kTwitchLimit;

    p.tags = extractTags(d.titleTemplate, d.notifTemplate);
    p.tagTotal = p.tags.size();
    p.tagsOver = p.tagTotal > kMaxTags;
    if (p.tagsOver) p.tags.resize(kMaxTags);
    return p;
}

Stream makeSample(int day, std::string date, const std::vector<std::string>& names,
                  const std::vector<std::string>& values) {
    Stream s;
    s.number = day;
    s.date = date.empty() ? todayIso() : std::move(date);
    for (std::size_t i = 0; i < names.size() && i < kMaxTags; ++i) {
        const bool has = i < values.size() && !values[i].empty();
        s.setTag(names[i], has ? values[i] : "<" + names[i] + ">");
    }
    return s;
}

const Series* Planner::findSeries(int id) const {
    for (const auto& s : store_.series) if (s.id == id) return &s;
    return nullptr;
}

const Stream* Planner::findStream(int id) const {
    for (const auto& t : store_.streams) if (t.id == id) return &t;
    return nullptr;
}

std::vector<const Stream*> Planner::streamsOf(int seriesId) const {
    std::vector<const Stream*> v;
    for (const auto& t : store_.streams) if (t.seriesId == seriesId) v.push_back(&t);
    std::sort(v.begin(), v.end(), [](const Stream* a, const Stream* b) {
        return a->number != b->number ? a->number < b->number : a->id < b->id;
    });
    return v;
}

Outcome Planner::persist(Outcome o) {
    if (!store_.save(paths_.db)) o.err = Err::StorageFailed;
    return o;
}

Outcome Planner::createSeries(const std::string& name) {
    Outcome o;
    o.id = store_.addSeries(name);
    return persist(o);
}

Outcome Planner::saveSeries(int id, const Draft& d, const Stream& reference) {
    Series* s = store_.findSeries(id);
    if (!s) return {Err::NoSeries};
    const Preview p = makePreview(d, reference);
    Outcome o;
    o.id = id;
    o.titleLen = p.titleLen;
    o.notifLen = p.notifLen;
    if (p.titleOver) { o.err = Err::TitleTooLong; return o; }
    if (p.notifOver) { o.err = Err::NotifTooLong; return o; }
    if (p.tagsOver) { o.err = Err::TooManyTags; return o; }
    s->name = d.name;
    s->titleTemplate = d.titleTemplate;
    s->notifTemplate = d.notifTemplate;
    return persist(o);
}

Outcome Planner::toggleActive(int id) {
    Series* s = store_.findSeries(id);
    if (!s) return {Err::NoSeries};
    s->active = !s->active;
    Outcome o;
    o.id = id;
    return persist(o);
}

Outcome Planner::deleteSeries(int id) {
    if (!store_.findSeries(id)) return {Err::NoSeries};
    store_.removeSeries(id);
    return persist();
}

Outcome Planner::addStream(int seriesId, int day, std::string date, std::vector<TagValue> tags) {
    Series* s = store_.findSeries(seriesId);
    if (!s) return {Err::NoSeries};
    if (day <= 0) return {Err::InvalidDay};
    if (date.empty()) date = todayIso();

    Stream probe;
    probe.number = day;
    probe.date = date;
    probe.tags = tags;
    Outcome o;
    o.titleLen = utf8Length(render(s->titleTemplate, *s, probe));
    o.notifLen = utf8Length(render(s->notifTemplate, *s, probe));
    if (o.titleLen > kTwitchLimit) { o.err = Err::TitleTooLong; return o; }
    if (o.notifLen > kTwitchLimit) { o.err = Err::NotifTooLong; return o; }

    o.id = store_.addStream(seriesId, day, date, std::move(tags));
    return persist(o);
}

Outcome Planner::moveStream(int streamId, int dir) {
    Stream* t = store_.findStream(streamId);
    if (!t) return {Err::NoStream};
    const int ns = static_cast<int>(t->status) + dir;
    Outcome o;
    o.id = streamId;
    if (ns < 0 || ns > 2) return o;  // ya está en el extremo: no es un error
    t->status = static_cast<Status>(ns);
    return persist(o);
}

Outcome Planner::deleteStream(int streamId) {
    if (!store_.findStream(streamId)) return {Err::NoStream};
    store_.removeStream(streamId);
    return persist();
}

const StreamTarget* Planner::target(int streamId, std::string_view platform) const {
    const Stream* t = findStream(streamId);
    return t ? t->findTarget(platform) : nullptr;
}

Outcome Planner::saveTarget(int streamId, const StreamTarget& target) {
    Stream* t = store_.findStream(streamId);
    if (!t) return {Err::NoStream};
    StreamTarget& dst = t->target(target.platform);
    dst = target;
    Outcome o;
    o.id = streamId;
    return persist(o);
}

}  // namespace sp
