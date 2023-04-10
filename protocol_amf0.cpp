#include "protocol_amf0.h"
#include "error.h"
#include "protocol_json.h"
#include "buffer.h"
#include <inttypes.h>
#include <cstring>
#include <sstream>

using namespace internal;

// AMF0 marker
#define RTMP_AMF0_Number                     0x00
#define RTMP_AMF0_Boolean                     0x01
#define RTMP_AMF0_String                     0x02
#define RTMP_AMF0_Object                     0x03
#define RTMP_AMF0_MovieClip                 0x04 // reserved, not supported
#define RTMP_AMF0_Null                         0x05
#define RTMP_AMF0_Undefined                 0x06
#define RTMP_AMF0_Reference                 0x07
#define RTMP_AMF0_EcmaArray                 0x08
#define RTMP_AMF0_ObjectEnd                 0x09
#define RTMP_AMF0_StrictArray                 0x0A
#define RTMP_AMF0_Date                         0x0B
#define RTMP_AMF0_LongString                 0x0C
#define RTMP_AMF0_UnSupported                 0x0D
#define RTMP_AMF0_RecordSet                 0x0E // reserved, not supported
#define RTMP_AMF0_XmlDocument                 0x0F
#define RTMP_AMF0_TypedObject                 0x10
// AVM+ object is the AMF3 object.
#define RTMP_AMF0_AVMplusObject             0x11
// origin array whos data takes the same form as LengthValueBytes
#define RTMP_AMF0_OriginStrictArray         0x20

// User defined
#define RTMP_AMF0_Invalid                     0x3F


Amf0Any::Amf0Any()
{
    m_marker = RTMP_AMF0_Invalid;
}

Amf0Any::~Amf0Any()
{

}

bool Amf0Any::IsString()
{
    return m_marker == RTMP_AMF0_String;
}

bool Amf0Any::IsBoolean()
{
    return m_marker == RTMP_AMF0_Boolean;
}

bool Amf0Any::IsNumber()
{
    return m_marker == RTMP_AMF0_Number;
}

bool Amf0Any::IsNull()
{
    return m_marker == RTMP_AMF0_Null;
}

bool Amf0Any::IsUndefined()
{
    return m_marker == RTMP_AMF0_Undefined;
}

bool Amf0Any::IsObject()
{
    return m_marker == RTMP_AMF0_Object;
}

bool Amf0Any::IsObjectEof()
{
    return m_marker == RTMP_AMF0_ObjectEnd;
}

bool Amf0Any::IsEcmaArray()
{
    return m_marker == RTMP_AMF0_EcmaArray;
}

bool Amf0Any::IsStrictArray()
{
    return m_marker == RTMP_AMF0_StrictArray;
}

bool Amf0Any::IsDate()
{
    return m_marker == RTMP_AMF0_Date;
}

bool Amf0Any::IsComplexObject()
{
    return IsObject() || IsObjectEof() || IsEcmaArray() || IsStrictArray();
}

std::string Amf0Any::ToStr()
{
    Amf0String* p = dynamic_cast<Amf0String*>(this);
    Assert(p != nullptr);
    return p->m_value;
}

const char *Amf0Any::ToStrRaw()
{
    Amf0String* p = dynamic_cast<Amf0String*>(this);
    Assert(p != nullptr);
    return p->m_value.data();
}

bool Amf0Any::ToBoolean()
{
    Amf0Boolean* p = dynamic_cast<Amf0Boolean*>(this);
    Assert(p != nullptr);
    return p->m_value;
}

double Amf0Any::ToNumber()
{
    Amf0Number* p = dynamic_cast<Amf0Number*>(this);
    Assert(p != NULL);
    return p->m_value;
}

int64_t Amf0Any::ToDate()
{
    Amf0Date* p = dynamic_cast<Amf0Date*>(this);
    Assert(p != NULL);
    return p->Date();
}

int16_t Amf0Any::ToDateTimeZone()
{
    Amf0Date* p = dynamic_cast<Amf0Date*>(this);
    Assert(p != NULL);
    return p->TimeZone();
}

Amf0Object *Amf0Any::ToObject()
{
    Amf0Object* p = dynamic_cast<Amf0Object*>(this);
    Assert(p != NULL);
    return p;
}

Amf0EcmaArray *Amf0Any::ToEcmaArray()
{
    Amf0EcmaArray* p = dynamic_cast<Amf0EcmaArray*>(this);
    Assert(p != NULL);
    return p;
}

Amf0StrictArray *Amf0Any::ToStrictArray()
{
    Amf0StrictArray* p = dynamic_cast<Amf0StrictArray*>(this);
    Assert(p != NULL);
    return p;
}

void Amf0Any::SetNumber(double value)
{
    Amf0Number* p = dynamic_cast<Amf0Number*>(this);
    Assert(p != NULL);
    p->m_value = value;
}

void FillLevelSpaces(std::stringstream& ss, int level)
{
    for (int i = 0; i < level; i++) {
        ss << "    ";
    }
}
void srs_amf0_do_print(Amf0Any* any, std::stringstream& ss, int level)
{
    std::ios_base::fmtflags oflags = ss.flags();

    if (any->IsBoolean()) {
        ss << "Boolean " << (any->ToBoolean()? "true":"false") << std::endl;
    } else if (any->IsNumber()) {
        ss << "Number " << std::fixed << any->ToNumber() << std::endl;
    } else if (any->IsString()) {
        ss << "String " << any->ToStr() << std::endl;
    } else if (any->IsDate()) {
        ss << "Date " << std::hex << any->ToDate()
           << "/" << std::hex << any->ToDateTimeZone() << std::endl;
    } else if (any->IsNull()) {
        ss << "Null" << std::endl;
    } else if (any->IsUndefined()) {
        ss << "Undefined" << std::endl;
    } else if (any->IsEcmaArray()) {
        Amf0EcmaArray* obj = any->ToEcmaArray();
        ss << "EcmaArray " << "(" << obj->Count() << " items)" << std::endl;
        for (int i = 0; i < obj->Count(); i++) {
            FillLevelSpaces(ss, level + 1);
            ss << "Elem '" << obj->KeyAt(i) << "' ";
            if (obj->ValueAt(i)->IsComplexObject()) {
                srs_amf0_do_print(obj->ValueAt(i), ss, level + 1);
            } else {
                srs_amf0_do_print(obj->ValueAt(i), ss, 0);
            }
        }
    } else if (any->IsStrictArray()) {
        Amf0StrictArray* obj = any->ToStrictArray();
        ss << "StrictArray " << "(" << obj->Count() << " items)" << std::endl;
        for (int i = 0; i < obj->Count(); i++) {
            FillLevelSpaces(ss, level + 1);
            ss << "Elem ";
            if (obj->At(i)->IsComplexObject()) {
                srs_amf0_do_print(obj->At(i), ss, level + 1);
            } else {
                srs_amf0_do_print(obj->At(i), ss, 0);
            }
        }
    } else if (any->IsObject()) {
        Amf0Object* obj = any->ToObject();
        ss << "Object " << "(" << obj->Count() << " items)" << std::endl;
        for (int i = 0; i < obj->Count(); i++) {
            FillLevelSpaces(ss, level + 1);
            ss << "Property '" << obj->KeyAt(i) << "' ";
            if (obj->ValueAt(i)->IsComplexObject()) {
                srs_amf0_do_print(obj->ValueAt(i), ss, level + 1);
            } else {
                srs_amf0_do_print(obj->ValueAt(i), ss, 0);
            }
        }
    } else {
        ss << "Unknown" << std::endl;
    }

    ss.flags(oflags);
}

char *Amf0Any::HumanPrint(char **pdata, int *psize)
{
    std::stringstream ss;

    ss.precision(1);

    srs_amf0_do_print(this, ss, 0);

    std::string str = ss.str();
    if (str.empty()) {
        return NULL;
    }

    char* data = new char[str.length() + 1];
    memcpy(data, str.data(), str.length());
    data[str.length()] = 0;

    if (pdata) {
        *pdata = data;
    }
    if (psize) {
        *psize = (int)str.length();
    }

    return data;
}

JsonAny *Amf0Any::ToJson()
{
    switch (m_marker) {
    case RTMP_AMF0_String: {
        return JsonAny::Str(ToStr().c_str());
    }
    case RTMP_AMF0_Boolean: {
        return JsonAny::Boolean(ToBoolean());
    }
    case RTMP_AMF0_Number: {
        double dv = ToNumber();
        int64_t iv = (int64_t)dv;
        if (iv == dv) {
            return JsonAny::Integer(iv);
        } else {
            return JsonAny::Number(dv);
        }
    }
    case RTMP_AMF0_Null: {
        return JsonAny::Null();
    }
    case RTMP_AMF0_Undefined: {
        return JsonAny::Null();
    }
    case RTMP_AMF0_Object: {
        // amf0 object implements it.
        Assert(false);
    }
    case RTMP_AMF0_EcmaArray: {
        // amf0 ecma array implements it.
        Assert(false);
    }
    case RTMP_AMF0_StrictArray: {
        // amf0 strict array implements it.
        Assert(false);
    }
    case RTMP_AMF0_Date: {
        // TODO: FIXME: implements it.
        return JsonAny::Null();
    }
    default: {
        return JsonAny::Null();
    }
    }
}

Amf0Any *Amf0Any::Str(const char *value)
{
    return new Amf0String(value);
}

Amf0Any *Amf0Any::Boolean(bool value)
{
    return new Amf0Boolean(value);
}

Amf0Any *Amf0Any::Number(double value)
{
    return new Amf0Number(value);
}

Amf0Any *Amf0Any::Date(int64_t value)
{
    return new Amf0Date(value);
}

Amf0Any *Amf0Any::Null()
{
    return new Amf0Null();
}

Amf0Any *Amf0Any::Undefined()
{
    return new Amf0Undefined();
}

Amf0Object *Amf0Any::Object()
{
    return new Amf0Object();
}

Amf0Any *Amf0Any::ObjectEof()
{
    return new Amf0ObjectEOF();
}

Amf0EcmaArray *Amf0Any::EcmaArray()
{
    return new Amf0EcmaArray();
}

Amf0StrictArray *Amf0Any::StrictArray()
{
    return new Amf0StrictArray();
}

error Amf0Any::Discovery(Buffer *stream, Amf0Any **ppvalue)
{
    error err = SUCCESS;

    // detect the object-eof specially
    if (Amf0IsObjectEof(stream)) {
        *ppvalue = new Amf0ObjectEOF();
        return err;
    }

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "marker requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();

    // backward the 1byte marker.
    stream->Skip(-1);

    switch (marker) {
    case RTMP_AMF0_String: {
        *ppvalue = Amf0Any::Str();
        return err;
    }
    case RTMP_AMF0_Boolean: {
        *ppvalue = Amf0Any::Boolean();
        return err;
    }
    case RTMP_AMF0_Number: {
        *ppvalue = Amf0Any::Number();
        return err;
    }
    case RTMP_AMF0_Null: {
        *ppvalue = Amf0Any::Null();
        return err;
    }
    case RTMP_AMF0_Undefined: {
        *ppvalue = Amf0Any::Undefined();
        return err;
    }
    case RTMP_AMF0_Object: {
        *ppvalue = Amf0Any::Object();
        return err;
    }
    case RTMP_AMF0_EcmaArray: {
        *ppvalue = Amf0Any::EcmaArray();
        return err;
    }
    case RTMP_AMF0_StrictArray: {
        *ppvalue = Amf0Any::StrictArray();
        return err;
    }
    case RTMP_AMF0_Date: {
        *ppvalue = Amf0Any::Date();
        return err;
    }
    case RTMP_AMF0_Invalid:
    default: {
        return ERRORNEW(ERROR_RTMP_AMF0_INVALID, "invalid amf0 message, marker=%#x", marker);
    }
    }
}

UnSortedHashtable::UnSortedHashtable()
{

}

UnSortedHashtable::~UnSortedHashtable()
{
    Clear();
}

int UnSortedHashtable::Count()
{
    return (int)m_properties.size();
}

void UnSortedHashtable::Clear()
{
    std::vector<Amf0ObjectPropertyType>::iterator it;
    for (it = m_properties.begin(); it != m_properties.end(); ++it) {
        Amf0ObjectPropertyType& elem = *it;
        Amf0Any* any = elem.second;
        Freep(any);
    }
    m_properties.clear();
}

std::string UnSortedHashtable::KeyAt(int index)
{
    Assert(index < Count());
    Amf0ObjectPropertyType& elem = m_properties[index];
    return elem.first;
}

const char *UnSortedHashtable::KeyRawAt(int index)
{
    Assert(index < Count());
    Amf0ObjectPropertyType& elem = m_properties[index];
    return elem.first.data();
}

Amf0Any *UnSortedHashtable::ValueAt(int index)
{
    Assert(index < Count());
    Amf0ObjectPropertyType& elem = m_properties[index];
    return elem.second;
}

void UnSortedHashtable::Set(std::string key, Amf0Any *value)
{
    std::vector<Amf0ObjectPropertyType>::iterator it;

    for (it = m_properties.begin(); it != m_properties.end(); ++it) {
        Amf0ObjectPropertyType& elem = *it;
        std::string name = elem.first;
        Amf0Any* any = elem.second;

        if (key == name) {
            Freep(any);
            it = m_properties.erase(it);
            break;
        }
    }

    if (value) {
        m_properties.push_back(std::make_pair(key, value));
    }
}

Amf0Any *UnSortedHashtable::GetProperty(std::string name)
{
    std::vector<Amf0ObjectPropertyType>::iterator it;

    for (it = m_properties.begin(); it != m_properties.end(); ++it) {
        Amf0ObjectPropertyType& elem = *it;
        std::string key = elem.first;
        Amf0Any* any = elem.second;
        if (key == name) {
            return any;
        }
    }

    return nullptr;
}

Amf0Any *UnSortedHashtable::EnsurePropertyString(std::string name)
{
    Amf0Any* prop = GetProperty(name);

    if (!prop) {
        return NULL;
    }

    if (!prop->IsString()) {
        return NULL;
    }

    return prop;
}

Amf0Any *UnSortedHashtable::EnsurePropertyNumber(std::string name)
{
    Amf0Any* prop = GetProperty(name);

    if (!prop) {
        return NULL;
    }

    if (!prop->IsNumber()) {
        return NULL;
    }

    return prop;
}

void UnSortedHashtable::Remove(std::string name)
{
    std::vector<Amf0ObjectPropertyType>::iterator it;

    for (it = m_properties.begin(); it != m_properties.end();) {
        std::string key = it->first;
        Amf0Any* any = it->second;

        if (key == name) {
            Freep(any);

            it = m_properties.erase(it);
        } else {
            ++it;
        }
    }
}

void UnSortedHashtable::Copy(UnSortedHashtable *src)
{
    std::vector<Amf0ObjectPropertyType>::iterator it;
    for (it = src->m_properties.begin(); it != src->m_properties.end(); ++it) {
        Amf0ObjectPropertyType& elem = *it;
        std::string key = elem.first;
        Amf0Any* any = elem.second;
        Set(key, any->Copy());
    }
}

Amf0ObjectEOF::Amf0ObjectEOF()
{
    m_marker = RTMP_AMF0_ObjectEnd;
}

Amf0ObjectEOF::~Amf0ObjectEOF()
{

}

int Amf0ObjectEOF::TotalSize()
{
    return Amf0Size::ObjectEof();
}

error Amf0ObjectEOF::Read(Buffer *stream)
{
    error err = SUCCESS;

    // value
    if (!stream->Require(2)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "EOF requires 2 only %d bytes", stream->Remain());
    }
    int16_t temp = stream->Read2Bytes();
    if (temp != 0x00) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "EOF invalid marker=%#x", temp);
    }

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "EOF requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_ObjectEnd) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "EOF invalid marker=%#x", marker);
    }

    return err;
}

error Amf0ObjectEOF::Write(Buffer *stream)
{
    error err = SUCCESS;

    // value
    if (!stream->Require(2)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "EOF requires 2 only %d bytes", stream->Remain());
    }
    stream->Write2Bytes(0x00);

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "EOF requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_ObjectEnd);

    return err;
}

Amf0Any *Amf0ObjectEOF::Copy()
{
    return new Amf0ObjectEOF();
}

Amf0Object::Amf0Object()
{
    m_properties = new UnSortedHashtable();
    m_eof = new Amf0ObjectEOF();
    m_marker = RTMP_AMF0_Object;
}

Amf0Object::~Amf0Object()
{
    Freep(m_properties);
    Freep(m_eof);
}

int Amf0Object::TotalSize()
{
    int size = 1;

    for (int i = 0; i < m_properties->Count(); i++){
        std::string name = KeyAt(i);
        Amf0Any* value = ValueAt(i);

        size += Amf0Size::Utf8(name);
        size += Amf0Size::Any(value);
    }

    size += Amf0Size::ObjectEof();

    return size;
}

error Amf0Object::Read(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "object requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_Object) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "object invalid marker=%#x", marker);
    }

    // value
    while (!stream->Empty()) {
        // detect whether is eof.
        if (Amf0IsObjectEof(stream)) {
            Amf0ObjectEOF pbj_eof;
            if ((err = pbj_eof.Read(stream)) != SUCCESS) {
                return ERRORWRAP(err, "read EOF");
            }
            break;
        }

        // property-name: utf8 string
        std::string property_name;
        if ((err = Amf0ReadUtf8(stream, property_name)) != SUCCESS) {
            return ERRORWRAP(err, "read property name");
        }
        // property-value: any
        Amf0Any* property_value = NULL;
        if ((err = Amf0ReadAny(stream, &property_value)) != SUCCESS) {
            Freep(property_value);
            return ERRORWRAP(err, "read property value, name=%s", property_name.c_str());
        }

        // add property
        this->Set(property_name, property_value);
    }

    return err;
}

error Amf0Object::Write(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "object requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_Object);

    // value
    for (int i = 0; i < m_properties->Count(); i++) {
        std::string name = this->KeyAt(i);
        Amf0Any* any = this->ValueAt(i);

        if ((err = Amf0WriteUtf8(stream, name)) != SUCCESS) {
            return ERRORWRAP(err, "write property name=%s", name.c_str());
        }

        if ((err = Amf0WriteAny(stream, any)) != SUCCESS) {
            return ERRORWRAP(err, "write property value, name=%s", name.c_str());
        }
    }

    if ((err = m_eof->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "write EOF");
    }

    return err;
}

Amf0Any *Amf0Object::Copy()
{
    Amf0Object* copy = new Amf0Object();
    copy->m_properties->Copy(m_properties);
    return copy;
}

JsonAny *Amf0Object::ToJson()
{
    JsonObject* obj = JsonAny::Object();

    for (int i = 0; i < m_properties->Count(); i++) {
        std::string name = this->KeyAt(i);
        Amf0Any* any = this->ValueAt(i);

        obj->Set(name, any->ToJson());
    }

    return obj;
}

void Amf0Object::Clear()
{
    m_properties->Clear();
}

int Amf0Object::Count()
{
    return m_properties->Count();
}

std::string Amf0Object::KeyAt(int index)
{
    return m_properties->KeyAt(index);
}

const char *Amf0Object::KeyRawAt(int index)
{
    return m_properties->KeyRawAt(index);
}

Amf0Any *Amf0Object::ValueAt(int index)
{
    return m_properties->ValueAt(index);
}

void Amf0Object::Set(std::string key, Amf0Any *value)
{
    m_properties->Set(key, value);
}

Amf0Any *Amf0Object::GetProperty(std::string name)
{
    return m_properties->GetProperty(name);
}

Amf0Any *Amf0Object::EnsurePropertyString(std::string name)
{
    return m_properties->EnsurePropertyString(name);
}

Amf0Any *Amf0Object::EnsurePropertyNumber(std::string name)
{
    return m_properties->EnsurePropertyNumber(name);
}

void Amf0Object::Remove(std::string name)
{
    m_properties->Remove(name);
}

Amf0EcmaArray::Amf0EcmaArray()
{
    m_count = 0;
    m_properties = new UnSortedHashtable();
    m_eof = new Amf0ObjectEOF;
    m_marker = RTMP_AMF0_EcmaArray;
}

Amf0EcmaArray::~Amf0EcmaArray()
{
    Freep(m_properties);
    Freep(m_eof);
}

int Amf0EcmaArray::TotalSize()
{
    int size = 1 + 4;

    for (int i = 0; i < m_properties->Count(); i++){
        std::string name = KeyAt(i);
        Amf0Any* value = ValueAt(i);

        size += Amf0Size::Utf8(name);
        size += Amf0Size::Any(value);
    }

    size += Amf0Size::ObjectEof();

    return size;
}

error Amf0EcmaArray::Read(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_EcmaArray) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "EcmaArray invalid marker=%#x", marker);
    }

    // count
    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 4 only %d bytes", stream->Remain());
    }

    int32_t count = stream->Read4Bytes();

    // value
    this->m_count = count;

    while (!stream->Empty()) {
        // detect whether is eof.
        if (Amf0IsObjectEof(stream)) {
            Amf0ObjectEOF pbj_eof;
            if ((err = pbj_eof.Read(stream)) != SUCCESS) {
                return ERRORWRAP(err, "read EOF");
            }
            break;
        }

        // property-name: utf8 string
        std::string property_name;
        if ((err =Amf0ReadUtf8(stream, property_name)) != SUCCESS) {
            return ERRORWRAP(err, "read property name");
        }
        // property-value: any
        Amf0Any* property_value = NULL;
        if ((err = Amf0ReadAny(stream, &property_value)) != SUCCESS) {
            return ERRORWRAP(err, "read property value, name=%s", property_name.c_str());
        }

        // add property
        this->Set(property_name, property_value);
    }

    return err;
}

error Amf0EcmaArray::Write(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_EcmaArray);

    // count
    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 4 only %d bytes", stream->Remain());
    }

    stream->Write4Bytes(this->m_count);

    // value
    for (int i = 0; i < m_properties->Count(); i++) {
        std::string name = this->KeyAt(i);
        Amf0Any* any = this->ValueAt(i);

        if ((err = Amf0WriteUtf8(stream, name)) != SUCCESS) {
            return ERRORWRAP(err, "write property name=%s", name.c_str());
        }

        if ((err = Amf0WriteAny(stream, any)) != SUCCESS) {
            return ERRORWRAP(err, "write property value, name=%s", name.c_str());
        }
    }

    if ((err = m_eof->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "write EOF");
    }

    return err;
}

Amf0Any *Amf0EcmaArray::Copy()
{
    Amf0EcmaArray* copy = new Amf0EcmaArray();
    copy->m_properties->Copy(m_properties);
    copy->m_count = m_count;
    return copy;
}

JsonAny *Amf0EcmaArray::ToJson()
{
    JsonObject* obj = JsonAny::Object();

    for (int i = 0; i < m_properties->Count(); i++) {
        std::string name = this->KeyAt(i);
        Amf0Any* any = this->ValueAt(i);

        obj->Set(name, any->ToJson());
    }

    return obj;
}

void Amf0EcmaArray::Clear()
{
    m_properties->Clear();
}

int Amf0EcmaArray::Count()
{
    return m_properties->Count();
}

std::string Amf0EcmaArray::KeyAt(int index)
{
    return m_properties->KeyAt(index);
}

const char *Amf0EcmaArray::KeyRawAt(int index)
{
    return m_properties->KeyRawAt(index);
}

Amf0Any *Amf0EcmaArray::ValueAt(int index)
{
    return m_properties->ValueAt(index);
}

void Amf0EcmaArray::Set(std::string key, Amf0Any *value)
{
    return m_properties->Set(key, value);
}

Amf0Any *Amf0EcmaArray::GetProperty(std::string name)
{
    return m_properties->GetProperty(name);
}

Amf0Any *Amf0EcmaArray::EnsurePropertyString(std::string name)
{
    return m_properties->EnsurePropertyString(name);
}

Amf0Any *Amf0EcmaArray::EnsurePropertyNumber(std::string name)
{
    return m_properties->EnsurePropertyNumber(name);
}

Amf0StrictArray::Amf0StrictArray()
{
    m_marker = RTMP_AMF0_StrictArray;
    m_count = 0;
}

Amf0StrictArray::~Amf0StrictArray()
{
    Clear();
}

int Amf0StrictArray::TotalSize()
{
    int size = 1 + 4;

    for (int i = 0; i < (int)m_properties.size(); i++){
        Amf0Any* any = m_properties[i];
        size += any->TotalSize();
    }

    return size;
}

error Amf0StrictArray::Read(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_StrictArray) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "StrictArray invalid marker=%#x", marker);
    }

    // count
    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 4 only %d bytes", stream->Remain());
    }

    int32_t count = stream->Read4Bytes();

    // value
    this->m_count = count;

    for (int i = 0; i < count && !stream->Empty(); i++) {
        // property-value: any
        Amf0Any* elem = NULL;
        if ((err = Amf0ReadAny(stream, &elem)) != SUCCESS) {
            return ERRORWRAP(err, "read property");
        }

        // add property
        m_properties.push_back(elem);
    }

    return err;
}

error Amf0StrictArray::Write(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_StrictArray);

    // count
    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 4 only %d bytes", stream->Remain());
    }

    stream->Write4Bytes(this->m_count);

    // value
    for (int i = 0; i < (int)m_properties.size(); i++) {
        Amf0Any* any = m_properties[i];

        if ((err = Amf0WriteAny(stream, any)) != SUCCESS) {
            return ERRORWRAP(err, "write property");
        }
    }

    return err;
}

Amf0Any *Amf0StrictArray::Copy()
{
    Amf0StrictArray* copy = new Amf0StrictArray();

    std::vector<Amf0Any*>::iterator it;
    for (it = m_properties.begin(); it != m_properties.end(); ++it) {
        Amf0Any* any = *it;
        copy->Append(any->Copy());
    }

    copy->m_count = m_count;
    return copy;
}

JsonAny *Amf0StrictArray::ToJson()
{
    JsonArray* arr = JsonAny::Array();

    for (int i = 0; i < (int)m_properties.size(); i++) {
        Amf0Any* any = m_properties[i];

        arr->Append(any->ToJson());
    }

    return arr;
}

void Amf0StrictArray::Clear()
{
    std::vector<Amf0Any*>::iterator it;
    for (it = m_properties.begin(); it != m_properties.end(); ++it) {
        Amf0Any* any = *it;
        Freep(any);
    }
    m_properties.clear();
}

int Amf0StrictArray::Count()
{
    return (int)m_properties.size();
}

Amf0Any *Amf0StrictArray::At(int index)
{
    Assert(index < (int)m_properties.size());
    return m_properties.at(index);
}

void Amf0StrictArray::Append(Amf0Any *any)
{
    m_properties.push_back(any);
    m_count = (int32_t)m_properties.size();
}

int Amf0Size::Utf8(std::string value)
{
    return (int)(2 + value.length());
}

int Amf0Size::Str(std::string value)
{
    return 1 + Amf0Size::Utf8(value);
}

int Amf0Size::Number()
{
    return 1 + 8;
}

int Amf0Size::Date()
{
    return 1 + 8 + 2;
}

int Amf0Size::Null()
{
    return 1;
}

int Amf0Size::Undefined()
{
    return 1;
}

int Amf0Size::Boolean()
{
    return 1 + 1;
}

int Amf0Size::Object(Amf0Object *obj)
{
    if (!obj) {
        return 0;
    }

    return obj->TotalSize();
}

int Amf0Size::ObjectEof()
{
    return 2 + 1;
}

int Amf0Size::EcmaArray(Amf0EcmaArray *arr)
{
    if (!arr) {
        return 0;
    }

    return arr->TotalSize();
}

int Amf0Size::StrictArray(Amf0StrictArray *arr)
{
    if (!arr) {
        return 0;
    }

    return arr->TotalSize();
}

int Amf0Size::Any(Amf0Any *o)
{
    if (!o) {
        return 0;
    }

    return o->TotalSize();
}

Amf0String::Amf0String(const char *_value)
{
    m_marker = RTMP_AMF0_String;
    if (_value) {
        m_value = _value;
    }
}

Amf0String::~Amf0String()
{

}

int Amf0String::TotalSize()
{
    return Amf0Size::Str(m_value);
}

error Amf0String::Read(Buffer *stream)
{
    return Amf0ReadString(stream, m_value);
}

error Amf0String::Write(Buffer *stream)
{
    return Amf0WriteString(stream, m_value);
}

Amf0Any *Amf0String::Copy()
{
    Amf0String* copy = new Amf0String(m_value.c_str());
    return copy;
}

Amf0Boolean::Amf0Boolean(bool _value)
{
    m_marker = RTMP_AMF0_Boolean;
    m_value = _value;
}

Amf0Boolean::~Amf0Boolean()
{

}

int Amf0Boolean::TotalSize()
{
    return Amf0Size::Boolean();
}

error Amf0Boolean::Read(Buffer *stream)
{
    return Amf0ReadBoolean(stream, m_value);
}

error Amf0Boolean::Write(Buffer *stream)
{
    return Amf0WriteBoolean(stream, m_value);
}

Amf0Any *Amf0Boolean::Copy()
{
    Amf0Boolean* copy = new Amf0Boolean(m_value);
    return copy;
}

Amf0Number::Amf0Number(double _value)
{
    m_marker = RTMP_AMF0_Number;
    m_value = _value;
}

Amf0Number::~Amf0Number()
{

}

int Amf0Number::TotalSize()
{
    return Amf0Size::Number();
}

error Amf0Number::Read(Buffer *stream)
{
    return Amf0ReadNumber(stream, m_value);
}

error Amf0Number::Write(Buffer *stream)
{
    return Amf0WriteNumber(stream, m_value);
}

Amf0Any *Amf0Number::Copy()
{
    Amf0Number* copy = new Amf0Number(m_value);
    return copy;
}

Amf0Date::Amf0Date(int64_t value)
{
    m_marker = RTMP_AMF0_Date;
    m_dateValue = value;
    m_timeZone = 0;
}

Amf0Date::~Amf0Date()
{

}

int Amf0Date::TotalSize()
{
    return Amf0Size::Date();
}

error Amf0Date::Read(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_Date) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "Date invalid marker=%#x", marker);
    }

    // date value
    // An ActionScript Date is serialized as the number of milliseconds
    // elapsed since the epoch of midnight on 1st Jan 1970 in the UTC
    // time zone.
    if (!stream->Require(8)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 8 only %d bytes", stream->Remain());
    }

    m_dateValue = stream->Read8Bytes();

    // time zone
    // While the design of this type reserves room for time zone offset
    // information, it should not be filled in, nor used, as it is unconventional
    // to change time zones when serializing dates on a network. It is suggested
    // that the time zone be queried independently as needed.
    if (!stream->Require(2)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 2 only %d bytes", stream->Remain());
    }

    m_timeZone = stream->Read2Bytes();

    return err;
}

error Amf0Date::Write(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_Date);

    // date value
    if (!stream->Require(8)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 8 only %d bytes", stream->Remain());
    }

    stream->Write8Bytes(m_dateValue);

    // time zone
    if (!stream->Require(2)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 2 only %d bytes", stream->Remain());
    }

    stream->Write2Bytes(m_timeZone);

    return err;
}

Amf0Any *Amf0Date::Copy()
{
    Amf0Date* copy = new Amf0Date(0);

    copy->m_dateValue = m_dateValue;
    copy->m_timeZone = m_timeZone;

    return copy;
}

int64_t Amf0Date::Date()
{
    return m_dateValue;
}

int16_t Amf0Date::TimeZone()
{
    return m_timeZone;
}

Amf0Null::Amf0Null()
{
    m_marker = RTMP_AMF0_Null;
}

Amf0Null::~Amf0Null()
{

}

int Amf0Null::TotalSize()
{
    return Amf0Size::Null();
}

error Amf0Null::Read(Buffer *stream)
{
    return Amf0ReadNull(stream);
}

error Amf0Null::Write(Buffer *stream)
{
    return Amf0WriteNull(stream);
}

Amf0Any *Amf0Null::Copy()
{
    Amf0Null* copy = new Amf0Null();
    return copy;
}

Amf0Undefined::Amf0Undefined()
{
    m_marker = RTMP_AMF0_Undefined;
}

Amf0Undefined::~Amf0Undefined()
{

}

int Amf0Undefined::TotalSize()
{
    return Amf0Size::Undefined();
}

error Amf0Undefined::Read(Buffer *stream)
{
    return Amf0ReadUndefined(stream);
}

error Amf0Undefined::Write(Buffer *stream)
{
    return Amf0WriteUndefined(stream);
}

Amf0Any *Amf0Undefined::Copy()
{
    Amf0Undefined* copy = new Amf0Undefined();
    return copy;
}

error Amf0ReadAny(Buffer *stream, Amf0Any **ppvalue)
{
    error err = SUCCESS;

    if ((err = Amf0Any::Discovery(stream, ppvalue)) != SUCCESS) {
        return ERRORWRAP(err, "discovery");
    }

    Assert(*ppvalue);

    if ((err = (*ppvalue)->Read(stream)) != SUCCESS) {
        Freep(*ppvalue);
        return ERRORWRAP(err, "parse elem");
    }

    return err;
}

error Amf0ReadString(Buffer *stream, std::string &value)
{
    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_String) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "String invalid marker=%#x", marker);
    }

    return Amf0ReadUtf8(stream, value);
}

error Amf0WriteString(Buffer *stream, std::string value)
{
    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_String);

    return Amf0WriteUtf8(stream, value);
}

error Amf0ReadBoolean(Buffer *stream, bool &value)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_Boolean) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "Boolean invalid marker=%#x", marker);
    }

    // value
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    value = (stream->Read1Bytes() != 0);

    return err;
}

error Amf0WriteBoolean(Buffer *stream, bool value)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }
    stream->Write1Bytes(RTMP_AMF0_Boolean);

    // value
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    if (value) {
        stream->Write1Bytes(0x01);
    } else {
        stream->Write1Bytes(0x00);
    }

    return err;
}

error Amf0ReadNumber(Buffer *stream, double &value)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_Number) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "Number invalid marker=%#x", marker);
    }

    // value
    if (!stream->Require(8)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 8 only %d bytes", stream->Remain());
    }

    int64_t temp = stream->Read8Bytes();
    memcpy(&value, &temp, 8);

    return err;
}

error Amf0WriteNumber(Buffer *stream, double value)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_Number);

    // value
    if (!stream->Require(8)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 8 only %d bytes", stream->Remain());
    }

    int64_t temp = 0x00;
    memcpy(&temp, &value, 8);
    stream->Write8Bytes(temp);

    return err;
}

error Amf0ReadNull(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_Null) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "Null invalid marker=%#x", marker);
    }

    return err;
}

error Amf0WriteNull(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_Null);

    return err;
}

error Amf0ReadUndefined(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->Remain());
    }

    char marker = stream->Read1Bytes();
    if (marker != RTMP_AMF0_Undefined) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "Undefined invalid marker=%#x", marker);
    }

    return err;
}

error Amf0WriteUndefined(Buffer *stream)
{
    error err = SUCCESS;

    // marker
    if (!stream->Require(1)) {
        return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->Remain());
    }

    stream->Write1Bytes(RTMP_AMF0_Undefined);

    return err;
}

namespace internal {
    error Amf0ReadUtf8(Buffer *stream, std::string &value)
    {
        error err = SUCCESS;

        // len
        if (!stream->Require(2)) {
            return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires 2 only %d bytes", stream->Remain());
        }
        int16_t len = stream->Read2Bytes();

        // empty string
        if (len <= 0) {
            return err;
        }

        // data
        if (!stream->Require(len)) {
            return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "requires %d only %d bytes", len, stream->Remain());
        }
        std::string str = stream->ReadString(len);

        // support utf8-1 only
        // 1.3.1 Strings and UTF-8
        // UTF8-1 = %x00-7F
        // TODO: support other utf-8 strings
        /*for (int i = 0; i < len; i++) {
         char ch = *(str.data() + i);
         if ((ch & 0x80) != 0) {
         ret = ERROR_RTMP_AMF0_DECODE;
         srs_error("ignored. only support utf8-1, 0x00-0x7F, actual is %#x. ret=%d", (int)ch, ret);
         ret = ERROR_SUCCESS;
         }
         }*/

        value = str;

        return err;
    }

    error Amf0WriteUtf8(Buffer *stream, std::string value)
    {
        error err = SUCCESS;

        // len
        if (!stream->Require(2)) {
            return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires 2 only %d bytes", stream->Remain());
        }
        stream->Write2Bytes((int16_t)value.length());

        // empty string
        if (value.length() <= 0) {
            return err;
        }

        // data
        if (!stream->Require((int)value.length())) {
            return ERRORNEW(ERROR_RTMP_AMF0_ENCODE, "requires %" PRIu64 " only %d bytes", value.length(), stream->Remain());
        }
        stream->WriteString(value);

        return err;
    }

    bool Amf0IsObjectEof(Buffer *stream)
    {
        // detect the object-eof specially
        if (stream->Require(3)) {
            int32_t flag = stream->Read3Bytes();
            stream->Skip(-3);

            return 0x09 == flag;
        }

        return false;
    }

    error Amf0WriteObjectEof(Buffer *stream, Amf0ObjectEOF *value)
    {
        Assert(value != NULL);
        return value->Write(stream);
    }

    error Amf0WriteAny(Buffer *stream, Amf0Any *value)
    {
        Assert(value != NULL);
        return value->Write(stream);
    }

}

