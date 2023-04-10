#include "app_fragment.h"
#include "error.h"
#include "log.h"
#include "utility.h"
#include <unistd.h>
#include <sstream>

Fragment::Fragment()
{
    m_dur = 0;
    m_startDts = -1;
    m_sequenceHeader = false;
}

Fragment::~Fragment()
{

}

void Fragment::Append(int64_t dts)
{
    // The max positive ms is 0x7fffffffffffffff/1000.
    static const int64_t maxMS = 0x20c49ba5e353f7LL;

    // We reset negative or overflow dts to zero.
    if (dts > maxMS || dts < 0) {
        dts = 0;
    }

    utime_t dts_in_tbn = dts * UTIME_MILLISECONDS;

    if (m_startDts == -1) {
        m_startDts = dts_in_tbn;
    }

    // TODO: FIXME: Use cumulus dts.
    m_startDts = MIN(m_startDts, dts_in_tbn);
    m_dur = dts_in_tbn - m_startDts;
}

utime_t Fragment::Duration()
{
    return m_dur;
}

bool Fragment::IsSequenceHeader()
{
    return m_sequenceHeader;
}

void Fragment::SetSequenceHeader(bool v)
{
    m_sequenceHeader = v;
}

std::string Fragment::Fullpath()
{
    return m_filepath;
}

void Fragment::SetPath(std::string v)
{
    m_filepath = v;
}

error Fragment::UnlinkFile()
{
    error err = SUCCESS;

    if (::unlink(m_filepath.c_str()) < 0) {
        return ERRORNEW(ERROR_SYSTEM_FRAGMENT_UNLINK, "unlink %s", m_filepath.c_str());
    }

    return err;
}

error Fragment::CreateDir()
{
    error err = SUCCESS;

    std::string segment_dir = PathDirname(m_filepath);

    if ((err = CreateDirRecursivel(segment_dir)) != SUCCESS) {
        return ERRORWRAP(err, "create %s", segment_dir.c_str());
    }

    info("Create dir %s ok", segment_dir.c_str());

    return err;
}

std::string Fragment::Tmppath()
{
    return m_filepath + ".tmp";
}

error Fragment::UnlinkTmpfile()
{
    error err = SUCCESS;

    std::string filepath = Tmppath();
    if (::unlink(m_filepath.c_str()) < 0) {
        return ERRORNEW(ERROR_SYSTEM_FRAGMENT_UNLINK, "unlink tmp file %s", m_filepath.c_str());
    }

    return err;
}

error Fragment::Rename()
{
    error err = SUCCESS;

    std::string full_path = Fullpath();
    std::string tmp_file = Tmppath();
    int tempdur = u2msi(Duration());
    if (true) {
       std::stringstream ss;
       ss << tempdur;
       full_path = StringReplace(full_path, "[duration]", ss.str());
    }

    int r0 = ::rename(tmp_file.c_str(), full_path.c_str());
    if (r0 < 0) {
        return ERRORNEW(ERROR_SYSTEM_FRAGMENT_RENAME, "rename %s to %s", tmp_file.c_str(), full_path.c_str());
    }

    m_filepath = full_path;
    return err;
}

FragmentWindow::FragmentWindow()
{

}

FragmentWindow::~FragmentWindow()
{
    std::vector<Fragment*>::iterator it;

    for (it = m_fragments.begin(); it != m_fragments.end(); ++it) {
        Fragment* fragment = *it;
        Freep(fragment);
    }
    m_fragments.clear();

    for (it = m_expiredFragments.begin(); it != m_expiredFragments.end(); ++it) {
        Fragment* fragment = *it;
        Freep(fragment);
    }
    m_expiredFragments.clear();
}

void FragmentWindow::Dispose()
{
    error err = SUCCESS;

    std::vector<Fragment*>::iterator it;

    for (it = m_fragments.begin(); it != m_fragments.end(); ++it) {
        Fragment* fragment = *it;
        if ((err = fragment->UnlinkFile()) != SUCCESS) {
            warn("Unlink ts failed %s", ERRORDESC(err).c_str());
            Freep(err);
        }
        Freep(fragment);
    }
    m_fragments.clear();

    for (it = m_expiredFragments.begin(); it != m_expiredFragments.end(); ++it) {
        Fragment* fragment = *it;
        if ((err = fragment->UnlinkFile()) != SUCCESS) {
            warn("Unlink ts failed %s", ERRORDESC(err).c_str());
            Freep(err);
        }
        Freep(fragment);
    }
    m_expiredFragments.clear();
}

void FragmentWindow::Append(Fragment *fragment)
{
    m_fragments.push_back(fragment);
}

void FragmentWindow::Shrink(utime_t window)
{
    utime_t duration = 0;

    int remove_index = -1;

    for (int i = (int)m_fragments.size() - 1; i >= 0; i--) {
        Fragment* fragment = m_fragments[i];
        duration += fragment->Duration();

        if (duration > window) {
            remove_index = i;
            break;
        }
    }

    for (int i = 0; i < remove_index && !m_fragments.empty(); i++) {
        Fragment* fragment = *m_fragments.begin();
        m_fragments.erase(m_fragments.begin());
        m_expiredFragments.push_back(fragment);
    }
}

void FragmentWindow::ClearExpired(bool delete_files)
{
    error err = SUCCESS;

    std::vector<Fragment*>::iterator it;

    for (it = m_expiredFragments.begin(); it != m_expiredFragments.end(); ++it) {
        Fragment* fragment = *it;
        if (delete_files && (err = fragment->UnlinkFile()) != SUCCESS) {
            warn("Unlink ts failed, %s", ERRORDESC(err).c_str());
            Freep(err);
        }
        Freep(fragment);
    }

    m_expiredFragments.clear();
}

utime_t FragmentWindow::MaxDuration()
{
    utime_t v = 0;

    std::vector<Fragment*>::iterator it;

    for (it = m_fragments.begin(); it != m_fragments.end(); ++it) {
        Fragment* fragment = *it;
        v = MAX(v, fragment->Duration());
    }

    return v;
}

bool FragmentWindow::Empty()
{
    return m_fragments.empty();
}

Fragment *FragmentWindow::First()
{
    return m_fragments.at(0);
}

int FragmentWindow::Size()
{
    return (int)m_fragments.size();
}

Fragment *FragmentWindow::At(int index)
{
    return m_fragments.at(index);
}
