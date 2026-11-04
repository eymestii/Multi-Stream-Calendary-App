#include "core/integrations/secure_file.h"

#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sp::integrations {

bool writePrivateFile(const std::filesystem::path& file, const std::string& data) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    auto tmp = file;
    tmp += ".tmp";
#ifndef _WIN32
    // El temporal nace ya con permisos 0600: el contenido nunca queda legible
    // por otros usuarios, ni siquiera durante la escritura.
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    // Si el temporal ya existía con permisos más amplios, se corrigen.
    ::fchmod(fd, S_IRUSR | S_IWUSR);
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written <= 0) {
            ::close(fd);
            ::unlink(tmp.c_str());
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    ::fsync(fd);
    ::close(fd);
#else
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
#endif
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(file, ec);
        ec.clear();
        std::filesystem::rename(tmp, file, ec);
    }
    return !ec;
}

}  // namespace sp::integrations
