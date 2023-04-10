#ifndef APP_FRAGMENT_H
#define APP_FRAGMENT_H


#include "core_time.h"
#include <vector>

// Represent a fragment, such as HLS segment, DVR segment or DASH segment.
// It's a media file, for example FLV or MP4, with duration.
class Fragment
{
private:
    // The duration in utime_t.
    utime_t m_dur;
    // The full file path of fragment.
    std::string m_filepath;
    // The start DTS in utime_t of segment.
    utime_t m_startDts;
    // Whether current segement contains sequence header.
    bool m_sequenceHeader;
public:
    Fragment();
    virtual ~Fragment();
public:
    // Append a frame with dts into fragment.
    // @dts The dts of frame in ms.
    virtual void Append(int64_t dts);
    // Get the duration of fragment in utime_t.
    virtual utime_t Duration();
    // Whether the fragment contains any sequence header.
    virtual bool IsSequenceHeader();
    // Set whether contains sequence header.
    virtual void SetSequenceHeader(bool v);
    // Get the full path of fragment.
    virtual std::string Fullpath();
    // Set the full path of fragment.
    virtual void SetPath(std::string v);
    // Unlink the fragment, to delete the file.
    // @remark Ignore any error.
    virtual error UnlinkFile();
    // Create the dir for file recursively.
    virtual error CreateDir();
public:
    // Get the temporary path for file.
    virtual std::string Tmppath();
    // Unlink the temporary file.
    virtual error UnlinkTmpfile();
    // Rename the temp file to final file.
    virtual error Rename();
};

// The fragment window manage a series of fragment.
class FragmentWindow
{
private:
    std::vector<Fragment*> m_fragments;
    // The expired fragments, need to be free in future.
    std::vector<Fragment*> m_expiredFragments;
public:
    FragmentWindow();
    virtual ~FragmentWindow();
public:
    // Dispose all fragments, delete the files.
    virtual void Dispose();
    // Append a new fragment, which is ready to delivery to client.
    virtual void Append(Fragment* fragment);
    // Shrink the window, push the expired fragment to a queue.
    virtual void Shrink(utime_t window);
    // Clear the expired fragments.
    virtual void ClearExpired(bool delete_files);
    // Get the max duration in utime_t of all fragments.
    virtual utime_t MaxDuration();
public:
    virtual bool Empty();
    virtual Fragment* First();
    virtual int Size();
    virtual Fragment* At(int index);
};
#endif // APP_FRAGMENT_H
