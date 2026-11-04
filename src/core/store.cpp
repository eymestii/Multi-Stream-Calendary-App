#include "core/store.h"

#include <algorithm>

namespace sp {

const std::string* Stream::findTag(std::string_view name) const {
    for (const auto& tv : tags)
        if (tv.name == name) return &tv.value;
    return nullptr;
}

void Stream::setTag(std::string_view name, std::string value) {
    for (auto& tv : tags)
        if (tv.name == name) { tv.value = std::move(value); return; }
    tags.push_back({std::string(name), std::move(value)});
}

const StreamTarget* Stream::findTarget(std::string_view platform) const {
    for (const auto& t : targets)
        if (t.platform == platform) return &t;
    return nullptr;
}

StreamTarget& Stream::target(std::string_view platform) {
    for (auto& t : targets)
        if (t.platform == platform) return t;
    StreamTarget created;
    created.platform = std::string(platform);
    targets.push_back(std::move(created));
    return targets.back();
}

int Store::addSeries(const std::string& name) {
    Series s;
    s.id = nextId_++;
    s.name = name;
    s.titleTemplate = "[{series}] Día {day} | {topic}";
    s.notifTemplate = "Hoy en directo: {series} día {day} - {topic}";
    series.push_back(std::move(s));
    return series.back().id;
}

int Store::addStream(int seriesId, int day, const std::string& date, std::vector<TagValue> tags) {
    Stream t;
    t.id = nextId_++;
    t.seriesId = seriesId;
    t.number = day;
    t.date = date;
    t.tags = std::move(tags);
    streams.push_back(std::move(t));
    return streams.back().id;
}

void Store::removeSeries(int id) {
    series.erase(std::remove_if(series.begin(), series.end(), [&](const Series& s) { return s.id == id; }), series.end());
    streams.erase(std::remove_if(streams.begin(), streams.end(), [&](const Stream& t) { return t.seriesId == id; }), streams.end());
    series.shrink_to_fit();
    streams.shrink_to_fit();
}

void Store::removeStream(int id) {
    streams.erase(std::remove_if(streams.begin(), streams.end(), [&](const Stream& t) { return t.id == id; }), streams.end());
    streams.shrink_to_fit();
}

Series* Store::findSeries(int id) {
    for (auto& s : series) if (s.id == id) return &s;
    return nullptr;
}

Stream* Store::findStream(int id) {
    for (auto& t : streams) if (t.id == id) return &t;
    return nullptr;
}

int Store::count(int seriesId, Status st) const {
    int n = 0;
    for (const auto& t : streams) if (t.seriesId == seriesId && t.status == st) ++n;
    return n;
}

int Store::latestDay(int seriesId) const {
    int m = 1;
    for (const auto& t : streams) if (t.seriesId == seriesId) m = std::max(m, t.number);
    return m;
}

}  // namespace sp
