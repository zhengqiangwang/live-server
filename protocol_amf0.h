#ifndef PROTOCOL_AMF0_H
#define PROTOCOL_AMF0_H


#include "log.h"
#include <string>
#include <vector>

class Buffer;
class Amf0Object;
class Amf0EcmaArray;
class Amf0StrictArray;
class JsonAny;

// internal objects, user should never use it.
namespace internal
{
    class UnSortedHashtable;
    class Amf0ObjectEOF;
    class Amf0Date;
}

/*
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 Usages:

 1. the bytes proxy: Buffer
 // when we got some bytes from file or network,
 // use Buffer proxy to read/write bytes

 // for example, read bytes from file or network.
 char* bytes = ...;

 // initialize the stream, proxy for bytes.
 Buffer stream;
 stream.Initialize(bytes);

 // use stream instead.

 2. directly read AMF0 any instance from stream:
 Amf0Any* pany = NULL;
 Amf0ReadAny(&stream, &pany);

 3. use Amf0Any to discovery instance from stream:
 Amf0Any* pany = NULL;
 Amf0Any::Discovery(&stream, &pany);

 4. directly read specified AMF0 instance value from stream:
 string value;
 Amf0ReadString(&stream, value);

 5. directly read specified AMF0 instance from stream:
 Amf0Any* str = Amf0Any::Str();
 str->Read(&stream);

 6. get value from AMF0 instance:
 // parse or set by other user
 Amf0Any* any = ...;

 if (any->IsString()) {
 string str = any->ToString();
 }

 7. get complex object from AMF0 insance:
 // parse or set by other user
 Amf0Any* any = ...;

 if (any->IsObject()) {
 Amf0Object* obj = any->ToObject();
 obj->Set("width", Amf0Any::Number(1024));
 obj->Set("height", Amf0Any::Number(576));
 }

 8. serialize AMF0 instance to bytes:
 // parse or set by other user
 Amf0Any* any = ...;

 char* bytes = new char[any->total_size()];

 Buffer stream;
 stream.Initialize(bytes);

 any->Write(&stream);

 @remark: for detail usage, see interfaces of each object.
 @remark: all examples ignore the error process.
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 */

/**
 * any amf0 value.
 * 2.1 Types Overview
 * value-type = number-type | boolean-type | string-type | object-type
 *         | null-marker | undefined-marker | reference-type | ecma-array-type
 *         | strict-array-type | date-type | long-string-type | xml-document-type
 *         | typed-object-type
 */
class Amf0Any
{
public:
    char m_marker;
public:
    Amf0Any();
    virtual ~Amf0Any();
    // type identify, user should identify the type then convert from/to value.
public:
    /**
     * whether current instance is an AMF0 string.
     * @return true if instance is an AMF0 string; otherwise, false.
     * @remark, if true, use ToString() to get its value.
     */
    virtual bool IsString();
    /**
     * whether current instance is an AMF0 boolean.
     * @return true if instance is an AMF0 boolean; otherwise, false.
     * @remark, if true, use ToBoolean() to get its value.
     */
    virtual bool IsBoolean();
    /**
     * whether current instance is an AMF0 number.
     * @return true if instance is an AMF0 number; otherwise, false.
     * @remark, if true, use ToNumber() to get its value.
     */
    virtual bool IsNumber();
    /**
     * whether current instance is an AMF0 null.
     * @return true if instance is an AMF0 null; otherwise, false.
     */
    virtual bool IsNull();
    /**
     * whether current instance is an AMF0 undefined.
     * @return true if instance is an AMF0 undefined; otherwise, false.
     */
    virtual bool IsUndefined();
    /**
     * whether current instance is an AMF0 object.
     * @return true if instance is an AMF0 object; otherwise, false.
     * @remark, if true, use ToObject() to get its value.
     */
    virtual bool IsObject();
    /**
     * whether current instance is an AMF0 object-EOF.
     * @return true if instance is an AMF0 object-EOF; otherwise, false.
     */
    virtual bool IsObjectEof();
    /**
     * whether current instance is an AMF0 ecma-array.
     * @return true if instance is an AMF0 ecma-array; otherwise, false.
     * @remark, if true, use ToEcmaArray() to get its value.
     */
    virtual bool IsEcmaArray();
    /**
     * whether current instance is an AMF0 strict-array.
     * @return true if instance is an AMF0 strict-array; otherwise, false.
     * @remark, if true, use ToStrictArray() to get its value.
     */
    virtual bool IsStrictArray();
    /**
     * whether current instance is an AMF0 date.
     * @return true if instance is an AMF0 date; otherwise, false.
     * @remark, if true, use ToDate() to get its value.
     */
    virtual bool IsDate();
    /**
     * whether current instance is an AMF0 object, object-EOF, ecma-array or strict-array.
     */
    virtual bool IsComplexObject();
    // get value of instance
public:
    /**
     * get a string copy of instance.
     * @remark assert IsString(), user must ensure the type then convert.
     */
    virtual std::string ToStr();
    /**
     * get the raw str of instance,
     * user can directly set the content of str.
     * @remark assert IsString(), user must ensure the type then convert.
     */
    virtual const char *ToStrRaw();
    /**
     * convert instance to amf0 boolean,
     * @remark assert IsBoolean(), user must ensure the type then convert.
     */
    virtual bool ToBoolean();
    /**
     * convert instance to amf0 number,
     * @remark assert IsNumber(), user must ensure the type then convert.
     */
    virtual double ToNumber();
    /**
     * convert instance to date,
     * @remark assert IsDate(), user must ensure the type then convert.
     */
    virtual int64_t ToDate();
    virtual int16_t ToDateTimeZone();
    /**
     * convert instance to amf0 object,
     * @remark assert IsObject(), user must ensure the type then convert.
     */
    virtual Amf0Object* ToObject();
    /**
     * convert instance to ecma array,
     * @remark assert IsEcmaArray(), user must ensure the type then convert.
     */
    virtual Amf0EcmaArray* ToEcmaArray();
    /**
     * convert instance to strict array,
     * @remark assert IsStrictArray(), user must ensure the type then convert.
     */
    virtual Amf0StrictArray* ToStrictArray();
    // set value of instance
public:
    /**
     * set the number of any when IsNumber() indicates true.
     * user must ensure the type is a number, or assert failed.
     */
    virtual void SetNumber(double value);
    // serialize/deseriaize instance.
public:
    /**
     * get the size of amf0 any, including the marker size.
     * the size is the bytes which instance serialized to.
     */
    virtual int TotalSize() = 0;
    /**
     * read AMF0 instance from stream.
     */
    virtual error Read(Buffer* stream) = 0;
    /**
     * write AMF0 instance to stream.
     */
    virtual error Write(Buffer* stream) = 0;
    /**
     * copy current AMF0 instance.
     */
    virtual Amf0Any* Copy() = 0;
    /**
     * human readable print
     * @param pdata, output the heap data, NULL to ignore.
     * @return return the *pdata for print. NULL to ignore.
     * @remark user must free the data returned or output by pdata.
     */
    virtual char* HumanPrint(char** pdata, int* psize);
    /**
     * convert amf0 to json.
     */
    virtual JsonAny* ToJson();
    // create AMF0 instance.
public:
    /**
     * create an AMF0 string instance, set string content by value.
     */
    static Amf0Any* Str(const char* value = NULL);
    /**
     * create an AMF0 boolean instance, set boolean content by value.
     */
    static Amf0Any* Boolean(bool value = false);
    /**
     * create an AMF0 number instance, set number content by value.
     */
    static Amf0Any* Number(double value = 0.0);
    /**
     * create an AMF0 date instance
     */
    static Amf0Any* Date(int64_t value = 0);
    /**
     * create an AMF0 null instance
     */
    static Amf0Any* Null();
    /**
     * create an AMF0 undefined instance
     */
    static Amf0Any* Undefined();
    /**
     * create an AMF0 empty object instance
     */
    static Amf0Object* Object();
    /**
     * create an AMF0 object-EOF instance
     */
    static Amf0Any* ObjectEof();
    /**
     * create an AMF0 empty ecma-array instance
     */
    static Amf0EcmaArray* EcmaArray();
    /**
     * create an AMF0 empty strict-array instance
     */
    static Amf0StrictArray* StrictArray();
    // discovery instance from stream
public:
    /**
     * discovery AMF0 instance from stream
     * @param ppvalue, output the discoveried AMF0 instance.
     *       NULL if error.
     * @remark, instance is created without read from stream, user must
     *       use (*ppvalue)->Read(stream) to get the instance.
     */
    static error Discovery(Buffer* stream, Amf0Any** ppvalue);
};

/**
 * 2.5 Object Type
 * anonymous-object-type = object-marker *(object-property)
 * object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
 */
class Amf0Object : public Amf0Any
{
private:
    internal::UnSortedHashtable* m_properties;
    internal::Amf0ObjectEOF* m_eof;
private:
    friend class Amf0Any;
    /**
     * make amf0 object to private,
     * use should never declare it, use Amf0Any::Object() to create it.
     */
    Amf0Object();
public:
    virtual ~Amf0Object();
    // serialize/deserialize to/from stream.
public:
    virtual int TotalSize();
    virtual error Read(Buffer* stream);
    virtual error Write(Buffer* stream);
    virtual Amf0Any* Copy();
    /**
     * convert amf0 to json.
     */
    virtual JsonAny* ToJson();
// properties iteration
public:
    /**
     * clear all propergies.
     */
    virtual void Clear();
    /**
     * get the count of properties(key:value).
     */
    virtual int Count();
    /**
     * get the property(key:value) key at index.
     * @remark: max index is Count().
     */
    virtual std::string KeyAt(int index);
    /**
     * get the property(key:value) key raw bytes at index.
     * user can directly set the key bytes.
     * @remark: max index is count().
     */
    virtual const char* KeyRawAt(int index);
    /**
     * get the property(key:value) value at index.
     * @remark: max index is count().
     */
    virtual Amf0Any* ValueAt(int index);
    // property set/get.
public:
    /**
     * set the property(key:value) of object,
     * @param key, string property name.
     * @param value, an AMF0 instance property value.
     * @remark user should never free the value, this instance will manage it.
     */
    virtual void Set(std::string key, Amf0Any* value);
    /**
     * get the property(key:value) of object,
     * @param name, the property name/key
     * @return the property AMF0 value, NULL if not found.
     * @remark user should never free the returned value, copy it if needed.
     */
    virtual Amf0Any* GetProperty(std::string name);
    /**
     * get the string property, ensure the property is_string().
     * @return the property AMF0 value, NULL if not found, or not a string.
     * @remark user should never free the returned value, copy it if needed.
     */
    virtual Amf0Any* EnsurePropertyString(std::string name);
    /**
     * get the number property, ensure the property is_number().
     * @return the property AMF0 value, NULL if not found, or not a number.
     * @remark user should never free the returned value, copy it if needed.
     */
    virtual Amf0Any* EnsurePropertyNumber(std::string name);
    /**
     * remove the property specified by name.
     */
    virtual void Remove(std::string name);
};

/**
 * 2.10 ECMA Array Type
 * ecma-array-type = associative-count *(object-property)
 * associative-count = U32
 * object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
 */
class Amf0EcmaArray : public Amf0Any
{
private:
    internal::UnSortedHashtable* m_properties;
    internal::Amf0ObjectEOF* m_eof;
    int32_t m_count;
private:
    friend class Amf0Any;
    /**
     * make amf0 object to private,
     * use should never declare it, use SrsAmf0Any::ecma_array() to create it.
     */
    Amf0EcmaArray();
public:
    virtual ~Amf0EcmaArray();
    // serialize/deserialize to/from stream.
public:
    virtual int TotalSize();
    virtual error Read(Buffer* stream);
    virtual error Write(Buffer* stream);
    virtual Amf0Any* Copy();
    /**
     * convert amf0 to json.
     */
    virtual JsonAny* ToJson();
// properties iteration
public:
    /**
     * clear all propergies.
     */
    virtual void Clear();
    /**
     * get the count of properties(key:value).
     */
    virtual int Count();
    /**
     * get the property(key:value) key at index.
     * @remark: max index is Count().
     */
    virtual std::string KeyAt(int index);
    /**
     * get the property(key:value) key raw bytes at index.
     * user can directly set the key bytes.
     * @remark: max index is Count().
     */
    virtual const char* KeyRawAt(int index);
    /**
     * get the property(key:value) value at index.
     * @remark: max index is Count().
     */
    virtual Amf0Any* ValueAt(int index);
    // property set/get.
public:
    /**
     * set the property(key:value) of array,
     * @param key, string property name.
     * @param value, an AMF0 instance property value.
     * @remark user should never free the value, this instance will manage it.
     */
    virtual void Set(std::string key, Amf0Any* value);
    /**
     * get the property(key:value) of array,
     * @param name, the property name/key
     * @return the property AMF0 value, NULL if not found.
     * @remark user should never free the returned value, copy it if needed.
     */
    virtual Amf0Any* GetProperty(std::string name);
    /**
     * get the string property, ensure the property is_string().
     * @return the property AMF0 value, NULL if not found, or not a string.
     * @remark user should never free the returned value, copy it if needed.
     */
    virtual Amf0Any* EnsurePropertyString(std::string name);
    /**
     * get the number property, ensure the property is_number().
     * @return the property AMF0 value, NULL if not found, or not a number.
     * @remark user should never free the returned value, copy it if needed.
     */
    virtual Amf0Any* EnsurePropertyNumber(std::string name);
};

/**
 * 2.12 Strict Array Type
 * array-count = U32
 * strict-array-type = array-count *(value-type)
 */
class Amf0StrictArray : public Amf0Any
{
private:
    std::vector<Amf0Any*> m_properties;
    int32_t m_count;
private:
    friend class Amf0Any;
    /**
     * make amf0 object to private,
     * use should never declare it, use SrsAmf0Any::strict_array() to create it.
     */
    Amf0StrictArray();
public:
    virtual ~Amf0StrictArray();
    // serialize/deserialize to/from stream.
public:
    virtual int TotalSize();
    virtual error Read(Buffer* stream);
    virtual error Write(Buffer* stream);
    virtual Amf0Any* Copy();
    /**
     * convert amf0 to json.
     */
    virtual JsonAny* ToJson();
// properties iteration
public:
    /**
     * clear all elements.
     */
    virtual void Clear();
    /**
     * get the count of elements
     */
    virtual int Count();
    /**
     * get the elements key at index.
     * @remark: max index is count().
     */
    virtual Amf0Any* At(int index);
    // property set/get.
public:
    /**
     * append new element to array
     * @param any, an AMF0 instance property value.
     * @remark user should never free the any, this instance will manage it.
     */
    virtual void Append(Amf0Any* any);
};

/**
 * the class to get amf0 object size
 */
class Amf0Size
{
public:
    static int Utf8(std::string value);
    static int Str(std::string value);
    static int Number();
    static int Date();
    static int Null();
    static int Undefined();
    static int Boolean();
    static int Object(Amf0Object* obj);
    static int ObjectEof();
    static int EcmaArray(Amf0EcmaArray* arr);
    static int StrictArray(Amf0StrictArray* arr);
    static int Any(Amf0Any* o);
};

/**
 * read anything from stream.
 * @param ppvalue, the output amf0 any elem.
 *         NULL if error; otherwise, never NULL and user must free it.
 */
extern error Amf0ReadAny(Buffer* stream, Amf0Any** ppvalue);

/**
 * read amf0 string from stream.
 * 2.4 String Type
 * string-type = string-marker UTF-8
 */
extern error Amf0ReadString(Buffer* stream, std::string& value);
extern error Amf0WriteString(Buffer* stream, std::string value);

/**
 * read amf0 boolean from stream.
 * 2.4 String Type
 * boolean-type = boolean-marker U8
 *         0 is false, <> 0 is true
 */
extern error Amf0ReadBoolean(Buffer* stream, bool& value);
extern error Amf0WriteBoolean(Buffer* stream, bool value);

/**
 * read amf0 number from stream.
 * 2.2 Number Type
 * number-type = number-marker DOUBLE
 */
extern error Amf0ReadNumber(Buffer* stream, double& value);
extern error Amf0WriteNumber(Buffer* stream, double value);

/**
 * read amf0 null from stream.
 * 2.7 null Type
 * null-type = null-marker
 */
extern error Amf0ReadNull(Buffer* stream);
extern error Amf0WriteNull(Buffer* stream);

/**
 * read amf0 undefined from stream.
 * 2.8 undefined Type
 * undefined-type = undefined-marker
 */
extern error Amf0ReadUndefined(Buffer* stream);
extern error Amf0WriteUndefined(Buffer* stream);

// internal objects, user should never use it.
namespace internal
{
    /**
     * read amf0 string from stream.
     * 2.4 String Type
     * string-type = string-marker UTF-8
     * @return default value is empty string.
     * @remark: use SrsAmf0Any::str() to create it.
     */
    class Amf0String : public Amf0Any
    {
    public:
        std::string m_value;
    private:
        friend class Amf0Any;
        /**
         * make amf0 string to private,
         * use should never declare it, use SrsAmf0Any::str() to create it.
         */
        Amf0String(const char* _value);
    public:
        virtual ~Amf0String();
    public:
        virtual int TotalSize();
        virtual error Read(Buffer* stream);
        virtual error Write(Buffer* stream);
        virtual Amf0Any* Copy();
    };

    /**
     * read amf0 boolean from stream.
     * 2.4 String Type
     * boolean-type = boolean-marker U8
     *         0 is false, <> 0 is true
     * @return default value is false.
     */
    class Amf0Boolean : public Amf0Any
    {
    public:
        bool m_value;
    private:
        friend class Amf0Any;
        /**
         * make amf0 boolean to private,
         * use should never declare it, use SrsAmf0Any::boolean() to create it.
         */
        Amf0Boolean(bool _value);
    public:
        virtual ~Amf0Boolean();
    public:
        virtual int TotalSize();
        virtual error Read(Buffer* stream);
        virtual error Write(Buffer* stream);
        virtual Amf0Any* Copy();
    };

    /**
     * read amf0 number from stream.
     * 2.2 Number Type
     * number-type = number-marker DOUBLE
     * @return default value is 0.
     */
    class Amf0Number : public Amf0Any
    {
    public:
        double m_value;
    private:
        friend class Amf0Any;
        /**
         * make amf0 number to private,
         * use should never declare it, use SrsAmf0Any::number() to create it.
         */
        Amf0Number(double _value);
    public:
        virtual ~Amf0Number();
    public:
        virtual int TotalSize();
        virtual error Read(Buffer* stream);
        virtual error Write(Buffer* stream);
        virtual Amf0Any* Copy();
    };

    /**
     * 2.13 Date Type
     * time-zone = S16 ; reserved, not supported should be set to 0x0000
     * date-type = date-marker DOUBLE time-zone
     * @see: https://github.com/ossrs/srs/issues/185
     */
    class Amf0Date : public Amf0Any
    {
    private:
        int64_t m_dateValue;
        int16_t m_timeZone;
    private:
        friend class Amf0Any;
        /**
         * make amf0 date to private,
         * use should never declare it, use SrsAmf0Any::date() to create it.
         */
        Amf0Date(int64_t value);
    public:
        virtual ~Amf0Date();
        // serialize/deserialize to/from stream.
    public:
        virtual int TotalSize();
        virtual error Read(Buffer* stream);
        virtual error Write(Buffer* stream);
        virtual Amf0Any* Copy();
    public:
        /**
         * get the date value.
         */
        virtual int64_t Date();
        /**
         * get the time_zone.
         */
        virtual int16_t TimeZone();
    };

    /**
     * read amf0 null from stream.
     * 2.7 null Type
     * null-type = null-marker
     */
    class Amf0Null : public Amf0Any
    {
    private:
        friend class Amf0Any;
        /**
         * make amf0 null to private,
         * use should never declare it, use SrsAmf0Any::null() to create it.
         */
        Amf0Null();
    public:
        virtual ~Amf0Null();
    public:
        virtual int TotalSize();
        virtual error Read(Buffer* stream);
        virtual error Write(Buffer* stream);
        virtual Amf0Any* Copy();
    };

    /**
     * read amf0 undefined from stream.
     * 2.8 undefined Type
     * undefined-type = undefined-marker
     */
    class Amf0Undefined : public Amf0Any
    {
    private:
        friend class Amf0Any;
        /**
         * make amf0 undefined to private,
         * use should never declare it, use SrsAmf0Any::undefined() to create it.
         */
        Amf0Undefined();
    public:
        virtual ~Amf0Undefined();
    public:
        virtual int TotalSize();
        virtual error Read(Buffer* stream);
        virtual error Write(Buffer* stream);
        virtual Amf0Any* Copy();
    };

    /**
     * to ensure in inserted order.
     * for the FMLE will crash when AMF0Object is not ordered by inserted,
     * if ordered in map, the string compare order, the FMLE will creash when
     * get the response of connect app.
     */
    class UnSortedHashtable
    {
    private:
        typedef std::pair<std::string, Amf0Any*> Amf0ObjectPropertyType;
        std::vector<Amf0ObjectPropertyType> m_properties;
    public:
        UnSortedHashtable();
        virtual ~UnSortedHashtable();
    public:
        virtual int Count();
        virtual void Clear();
        virtual std::string KeyAt(int index);
        virtual const char* KeyRawAt(int index);
        virtual Amf0Any* ValueAt(int index);
        /**
         * set the value of hashtable.
         * @param value, the value to set. NULL to delete the property.
         */
        virtual void Set(std::string key, Amf0Any* value);
    public:
        virtual Amf0Any* GetProperty(std::string name);
        virtual Amf0Any* EnsurePropertyString(std::string name);
        virtual Amf0Any* EnsurePropertyNumber(std::string name);
        virtual void Remove(std::string name);
    public:
        virtual void Copy(UnSortedHashtable* src);
    };

    /**
     * 2.11 Object End Type
     * object-end-type = UTF-8-empty object-end-marker
     * 0x00 0x00 0x09
     */
    class Amf0ObjectEOF : public Amf0Any
    {
    public:
        Amf0ObjectEOF();
        virtual ~Amf0ObjectEOF();
    public:
        virtual int TotalSize();
        virtual error Read(Buffer* stream);
        virtual error Write(Buffer* stream);
        virtual Amf0Any* Copy();
    };

    /**
     * read amf0 utf8 string from stream.
     * 1.3.1 Strings and UTF-8
     * UTF-8 = U16 *(UTF8-char)
     * UTF8-char = UTF8-1 | UTF8-2 | UTF8-3 | UTF8-4
     * UTF8-1 = %x00-7F
     * @remark only support UTF8-1 char.
     */
    extern error Amf0ReadUtf8(Buffer* stream, std::string& value);
    extern error Amf0WriteUtf8(Buffer* stream, std::string value);

    extern bool Amf0IsObjectEof(Buffer* stream);
    extern error Amf0WriteObjectEof(Buffer* stream, Amf0ObjectEOF* value);

    extern error Amf0WriteAny(Buffer* stream, Amf0Any* value);
};
#endif // PROTOCOL_AMF0_H
