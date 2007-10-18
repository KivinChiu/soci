// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#define SOCI_ODBC_SOURCE
#include "soci-odbc.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace soci;
using namespace soci::details;

void odbc_standard_use_type_backend::prepare_for_bind(
    void *&data, SQLUINTEGER &size, SQLSMALLINT &sqlType, SQLSMALLINT &cType)
{
    switch (type_)
    {
    // simple cases
    case eXShort:
        sqlType = SQL_SMALLINT;
        cType = SQL_C_SSHORT;
        size = sizeof(short);
        break;
    case eXInteger:
        sqlType = SQL_INTEGER;
        cType = SQL_C_SLONG;
        size = sizeof(int);
        break;
    case eXUnsignedLong:
        sqlType = SQL_BIGINT;
        cType = SQL_C_ULONG;
        size = sizeof(unsigned long);
        break;
    case eXDouble:
        sqlType = SQL_DOUBLE;
        cType = SQL_C_DOUBLE;
        size = sizeof(double);
        break;

    // cases that require adjustments and buffer management
    case eXChar:
        sqlType = SQL_CHAR;
        cType = SQL_C_CHAR;
        size = sizeof(char)+1;
        buf_ = new char[size];
        data = buf_;
        indHolder_ = SQL_NTS;
        break;
    case eXCString:
    {
        details::cstring_descriptor *desc = static_cast<cstring_descriptor *>(data);
        sqlType = SQL_VARCHAR;
        cType = SQL_C_CHAR;
        data = desc->str_;
        size = static_cast<SQLUINTEGER>(desc->bufSize_);
        indHolder_ = SQL_NTS;
    }
    break;
    case eXStdString:
    {
        // TODO: No textual value is assigned here!

        std::string* s = static_cast<std::string*>(data);
        sqlType = SQL_VARCHAR;
        cType = SQL_C_CHAR;
        size = 255; // !FIXME this is not sufficent
        buf_ = new char[size];
        data = buf_;
        indHolder_ = SQL_NTS;
    }
    break;
    case eXStdTm:
        sqlType = SQL_TIMESTAMP;
        cType = SQL_C_TIMESTAMP;
        buf_ = new char[sizeof(TIMESTAMP_STRUCT)];
        data = buf_;
        size = 19; // This number is not the size in bytes, but the number
                   // of characters in the date if it was written out
                   // yyyy-mm-dd hh:mm:ss
        break;

    case eXBLOB:
    {
//         sqlType = SQL_VARBINARY;
//         cType = SQL_C_BINARY;

//         BLOB *b = static_cast<BLOB *>(data);

//         odbc_blob_backend *bbe
//         = static_cast<odbc_blob_backend *>(b->getBackEnd());

//         size = 0;
//         indHolder_ = size;
        //TODO            data = &bbe->lobp_;
    }
    break;
    case eXStatement:
    case eXRowID:
        break;
    }
}

void odbc_standard_use_type_backend::bind_helper(int &position, void *data, eExchangeType type)
{
    data_ = data; // for future reference
    type_ = type; // for future reference

    SQLSMALLINT sqlType;
    SQLSMALLINT cType;
    SQLUINTEGER size;

    prepare_for_bind(data, size, sqlType, cType);

    SQLRETURN rc = SQLBindParameter(statement_.hstmt_,
                                    static_cast<SQLUSMALLINT>(position++),
                                    SQL_PARAM_INPUT,
                                    cType, sqlType, size, 0, data, 0, &indHolder_);

    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_STMT, statement_.hstmt_,
                                "Binding");
    }
}

void odbc_standard_use_type_backend::bind_by_pos(
    int &position, void *data, eExchangeType type)
{
    if (statement_.boundByName_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    bind_helper(position, data, type);

    statement_.boundByPos_ = true;
}

void odbc_standard_use_type_backend::bind_by_name(
    std::string const &name, void *data, eExchangeType type)
{
    if (statement_.boundByPos_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    int position = -1;
    int count = 1;

    for (std::vector<std::string>::iterator it = statement_.names_.begin();
         it != statement_.names_.end(); ++it)
    {
        if (*it == name)
        {
            position = count;
            break;
        }
        count++;
    }

    if (position != -1)
        bind_helper(position, data, type);
    else
    {
        std::ostringstream ss;
        ss << "Unable to find name '" << name << "' to bind to";
        throw soci_error(ss.str().c_str());
    }

    statement_.boundByName_ = true;
}

void odbc_standard_use_type_backend::pre_use(eIndicator const *ind)
{
    // first deal with data
    if (type_ == eXChar)
    {
        char *c = static_cast<char*>(data_);
        buf_[0] = *c;
        buf_[1] = '\0';
    }
    else if (type_ == eXStdString)
    {
        std::string *s = static_cast<std::string *>(data_);

        std::size_t const bufSize = 4000;
        std::size_t const sSize = s->size();
        std::size_t const toCopy =
            sSize < bufSize -1 ? sSize + 1 : bufSize - 1;
        strncpy(buf_, s->c_str(), toCopy);
        buf_[toCopy] = '\0';
    }
    else if (type_ == eXStdTm)
    {
        std::tm *t = static_cast<std::tm *>(data_);
        TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(buf_);

        ts->year = static_cast<SQLSMALLINT>(t->tm_year + 1900);
        ts->month = static_cast<SQLUSMALLINT>(t->tm_mon + 1);
        ts->day = static_cast<SQLUSMALLINT>(t->tm_mday);
        ts->hour = static_cast<SQLUSMALLINT>(t->tm_hour);
        ts->minute = static_cast<SQLUSMALLINT>(t->tm_min);
        ts->second = static_cast<SQLUSMALLINT>(t->tm_sec);
        ts->fraction = 0;
    }

    // then handle indicators
    if (ind != NULL && *ind == eNull)
    {
        indHolder_ = SQL_NULL_DATA; // null
    }
}

void odbc_standard_use_type_backend::post_use(bool gotData, eIndicator *ind)
{
    // first, deal with data
    if (gotData)
    {
        if (type_ == eXChar)
        {
            char *c = static_cast<char*>(data_);
            *c = buf_[0];
        }
        else if (type_ == eXStdString)
        {
            std::string *s = static_cast<std::string *>(data_);

            *s = buf_;
        }
        else if (type_ == eXStdTm)
        {
            std::tm *t = static_cast<std::tm *>(data_);
            TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(buf_);
            t->tm_isdst = -1;
            t->tm_year = ts->year - 1900;
            t->tm_mon = ts->month - 1;
            t->tm_mday = ts->day;
            t->tm_hour = ts->hour;
            t->tm_min = ts->minute;
            t->tm_sec = ts->second;

            // normalize and compute the remaining fields
            std::mktime(t);
        }
    }

    if (ind != NULL)
    {
        if (gotData == false)
        {
            *ind = eNoData;
        }
        else
        {
            if (indHolder_ == 0)
            {
                *ind = eOK;
            }
            else if (indHolder_ == SQL_NULL_DATA)
            {
                *ind = eNull;
            }
            else
            {
                *ind = eTruncated;
            }
        }
    }
    else
    {
        if (indHolder_ == SQL_NULL_DATA)
        {
            // fetched null and no indicator - programming error!
            throw soci_error("Null value fetched and no indicator defined.");
        }

        if (gotData == false)
        {
            // no data fetched and no indicator - programming error!
            throw soci_error("No data fetched and no indicator defined.");
        }
    }
}

void odbc_standard_use_type_backend::clean_up()
{
    if (buf_ != NULL)
    {
        delete [] buf_;
        buf_ = NULL;
    }
}
