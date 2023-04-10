#ifndef PROTOCOL_JSON_H
#define PROTOCOL_JSON_H

#include <string>
#include <vector>

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// JSON decode
// 1. JsonAny: read any from str:char*
//        JsonAny* any = NULL;
//        if ((any = JsonAny::loads(str)) == NULL) {
//            return -1;
//         }
//        Assert(pany); // if success, always valid object.
// 2. JsonAny: convert to specifid type, for instance, string
//        JsonAny* any = ...
//        if (any->is_string()) {
//            string v = any->to_str();
//        }
//
// For detail usage, see interfaces of each object.
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// @see: https://github.com/udp/json-parser

class Amf0Any;
class JsonArray;
class JsonObject;

class JsonAny
{
public:
    char m_marker;
    //don't directly create this object
    //please use JsonAny::str() to create a concreated one
protected:
    JsonAny();
public:
    virtual ~JsonAny();
public:
    virtual bool IsString();
    virtual bool IsBoolean();
    virtual bool IsInteger();
    virtual bool IsNumber();
    virtual bool IsObject();
    virtual bool IsArray();
    virtual bool IsNull();

public:
    //get the string of any when IsString() indicates true
    //user must ensure the type is a string, or assert failed.
    virtual std::string ToStr();
    // Get the boolean of any when IsBoolean() indicates true.
    // user must ensure the type is a boolean, or assert failed.
    virtual bool ToBoolean();
    // Get the integer of any when IsInteger() indicates true.
    // user must ensure the type is a integer, or assert failed.
    virtual int64_t ToInteger();
    // Get the number of any when IsNumber() indicates true.
    // user must ensure the type is a number, or assert failed.
    virtual double ToNumber();
    // Get the object of any when IsObject() indicates true.
    // user must ensure the type is a object, or assert failed.
    virtual JsonObject* ToObject();
    // Get the ecma array of any when IsEcmaArray() indicates true.
    // user must ensure the type is a ecma array, or assert failed.
    virtual JsonArray* ToArray();
public:
    virtual std::string Dumps();
    virtual Amf0Any* ToAmf0();
public:
    static JsonAny* Str(const char* value = nullptr);
    static JsonAny* Str(const char* value, int length);
    static JsonAny* Boolean(bool value = false);
    static JsonAny* Integer(int64_t value = 0);
    static JsonAny* Number(double value = 0.0);
    static JsonAny* Null();
    static JsonObject* Object();
    static JsonArray* Array();
public:
    //read json tree from string
    //@return json object, NULL is error
    static JsonAny* Loads(std::string str);
};

class JsonObject : public JsonAny
{
private:
    typedef std::pair<std::string, JsonAny*> JsonObjectPropertyType;
    std::vector<JsonObjectPropertyType> m_properties;
private:
    //use JsonAny::object() to create it
    friend class JsonAny;
    JsonObject();
public:
    virtual ~JsonObject();
public:
    virtual int Count();
    //max index is Count()
    virtual std::string KeyAt(int index);
    //max index is Count()
    virtual JsonAny* ValueAt(int index);
public:
    virtual std::string Dumps();
    virtual Amf0Any* ToAmf0();
public:
    virtual JsonObject* Set(std::string key, JsonAny* value);
    virtual JsonAny* GetProperty(std::string name);
    virtual JsonAny* EnsurePropertyString(std::string name);
    virtual JsonAny* EnsurePropertyInteger(std::string name);
    virtual JsonAny* EnsurePropertyNumber(std::string name);
    virtual JsonAny* EnsurePropertyBoolean(std::string name);
    virtual JsonAny* EnsurePropertyObject(std::string name);
    virtual JsonAny* EnsurePropertyArray(std::string name);
};

class JsonArray : public JsonAny
{
private:
    std::vector<JsonAny*> m_properties;

private:
    //use JsonAny::array() to create it
    friend class JsonAny;
    JsonArray();
public:
    virtual ~JsonArray();
public:
    virtual int Count();
    //max index is Count()
    virtual JsonAny* At(int index);
    virtual JsonArray* Add(JsonAny* value);
    // alias to add
    virtual JsonArray* Append(JsonAny* value);
public:
    virtual std::string Dumps();
    virtual Amf0Any* ToAmf0();
};

#endif // PROTOCOL_JSON_H
