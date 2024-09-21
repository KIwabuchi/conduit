// Copyright (c) Lawrence Livermore National Security, LLC and other Conduit
// Project developers. See top-level LICENSE AND COPYRIGHT files for dates and
// other details. No copyright assignment is required to contribute to Conduit.

//-----------------------------------------------------------------------------
///
/// file: conduit_schema.cpp
///
//-----------------------------------------------------------------------------
#include "conduit_schema.hpp"

//-----------------------------------------------------------------------------
// -- standard lib includes -- 
//-----------------------------------------------------------------------------
#include <stdio.h>

//-----------------------------------------------------------------------------
// -- conduit includes -- 
//-----------------------------------------------------------------------------
#include "conduit_generator.hpp"
#include "conduit_error.hpp"
#include "conduit_utils.hpp"


//-----------------------------------------------------------------------------
// -- begin conduit:: --
//-----------------------------------------------------------------------------
namespace conduit
{

//std::vector<std::string> Schema::m_empty_child_names;

//=============================================================================
//-----------------------------------------------------------------------------
//
//
// -- begin conduit::Schema public methods --
//
//
//-----------------------------------------------------------------------------
//=============================================================================

//----------------------------------------------------------------------------
//
/// Schema construction and destruction.
//
//----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
Schema::Schema(const allocator_type& allocator)
: m_allocator(allocator)
{
    init_defaults();
}

//---------------------------------------------------------------------------//
Schema::Schema(const Schema &schema)
: m_allocator(schema.m_allocator)
{
    init_defaults();
    set(schema);
}

//---------------------------------------------------------------------------//
Schema::Schema(index_t dtype_id, const allocator_type& allocator)
: m_allocator(allocator)
{
    init_defaults();
    set(dtype_id);
}

//---------------------------------------------------------------------------//
Schema::Schema(const DataType &dtype, const allocator_type& allocator)
: m_allocator(allocator)
{
    init_defaults();
    set(dtype);
}


//---------------------------------------------------------------------------//
Schema::Schema(const std::string &json_schema, const allocator_type& allocator)
: m_allocator(allocator)
{
    init_defaults();
    set(json_schema);
}

//---------------------------------------------------------------------------//
Schema::Schema(const char *json_schema, const allocator_type& allocator)
: m_allocator(allocator)
{
    init_defaults();
    set(std::string(json_schema));
}


//---------------------------------------------------------------------------//
Schema::~Schema()
{release();}

//---------------------------------------------------------------------------//
void
Schema::reset()
{
    release();
}

//-----------------------------------------------------------------------------
//
// Schema set methods
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
void 
Schema::set(const Schema &schema)
{
    reset();
    m_allocator = schema.m_allocator;

    bool init_children = false;
    index_t dt_id = schema.m_dtype.id();
    if (dt_id == DataType::OBJECT_ID)
    {
       init_object();
       init_children = true;

       object_map()   = schema.object_map();
       object_order() = schema.object_order();
    } 
    else if (dt_id == DataType::LIST_ID)
    {
       init_list();
       init_children = true;
    }
    else 
    {
        m_dtype = schema.m_dtype;
    }

    
    if (init_children) 
    {
       Schema::schema_vector_type &my_children = children();
       const Schema::schema_vector_type &their_children = schema.children();
       for (size_t i = 0; i < their_children.size(); i++) 
       {
           Schema *child_schema = allocate<Schema>(*their_children[i]);
           child_schema->m_parent = this;
           my_children.push_back(child_schema);
       }
    }
}


//---------------------------------------------------------------------------//
void 
Schema::set(index_t dtype_id)
{
    reset();
    m_dtype.reset();
    m_dtype.set_id(dtype_id);
}


//---------------------------------------------------------------------------//
void 
Schema::set(const DataType &dtype)
{
    reset();
    if (dtype.id() == DataType::OBJECT_ID) {
        init_object();
    } else if (dtype.id() == DataType::LIST_ID) {
        init_list();
    }
    m_dtype = dtype;
}

//---------------------------------------------------------------------------//
void 
Schema::set(const std::string &json_schema)
{
    reset();
    walk_schema(json_schema);
}


//-----------------------------------------------------------------------------
//
/// Schema assignment operators
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
Schema &
Schema::operator=(const Schema &schema)
{
    if(this != &schema)
    {
        set(schema);
    }
    return *this;
}

//---------------------------------------------------------------------------//
Schema &
Schema::operator=(const DataType &dtype)
{
    set(dtype);
    return *this;
}


//---------------------------------------------------------------------------//
Schema &
Schema::operator=(const std::string &json_schema)
{
    set(json_schema);
    return *this;
}

//-----------------------------------------------------------------------------
//
/// Information Methods
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
index_t
Schema::total_strided_bytes() const
{
    index_t res = 0;
    index_t dt_id = m_dtype.id();
    if(dt_id == DataType::OBJECT_ID || dt_id == DataType::LIST_ID)
    {
        const Schema::schema_vector_type &lst = children();
        for (Schema::schema_vector_type::const_iterator itr = lst.begin();
             itr < lst.end(); ++itr)
        {
            res += (*itr)->total_strided_bytes();
        }
    }
    else if (dt_id != DataType::EMPTY_ID)
    {
        res = m_dtype.strided_bytes();
    }
    return res;
}

//---------------------------------------------------------------------------//
index_t
Schema::total_bytes_compact() const
{
    index_t res = 0;
    index_t dt_id = m_dtype.id();
    if(dt_id == DataType::OBJECT_ID || dt_id == DataType::LIST_ID)
    {
        const Schema::schema_vector_type &lst = children();
        for (Schema::schema_vector_type::const_iterator itr = lst.begin();
             itr < lst.end(); ++itr)
        {
            res += (*itr)->total_bytes_compact();
        }
    }
    else if (dt_id != DataType::EMPTY_ID)
    {
        res = m_dtype.bytes_compact();
    }
    return res;
}

//---------------------------------------------------------------------------//
bool
Schema::is_compact() const
{
    return total_bytes_compact() == total_strided_bytes();
}

//---------------------------------------------------------------------------//
index_t
Schema::spanned_bytes() const
{
    index_t res = 0;

    index_t dt_id = m_dtype.id();
    if(dt_id == DataType::OBJECT_ID || dt_id == DataType::LIST_ID)
    {
        const Schema::schema_vector_type &lst = children();
        for (Schema::schema_vector_type::const_iterator itr = lst.begin();
             itr < lst.end(); ++itr)
        {
            // spanned bytes is the max of the spanned bytes of 
            // all children
            index_t curr_span = (*itr)->spanned_bytes();
            if(curr_span > res)
            {
                res = curr_span;
            }
        }
    }
    else
    {
        res = m_dtype.spanned_bytes();
    }
    return res;
}


//---------------------------------------------------------------------------//
bool
Schema::compatible(const Schema &s) const
{
    index_t dt_id   = m_dtype.id();
    index_t s_dt_id = s.dtype().id();

    if(dt_id != s_dt_id)
        return false;
    
    bool res = true;
    
    if(dt_id == DataType::OBJECT_ID)
    {
        // each of s's entries that match paths must have dtypes that match
        
        for(auto loc  = s.object_map().begin();
            loc != s.object_map().end() && res;
            loc++)
        {
            // make sure we actually have the path
            if(has_path(s.object_map().key(loc).data()))
            {
                // use index to fetch the child from the other schema
                const Schema &s_chld = s.child(s.object_map().value(loc));
                // fetch our child by name
                const Schema &chld = child(s.object_map().key(loc).data());
                // do compat check
                res = chld.compatible(s_chld);
            }
        }
    }
    else if(dt_id == DataType::LIST_ID) 
    {
        // each of s's entries dtypes must match
        index_t s_n_chd = s.number_of_children();
        
        // can't be compatible in this case
        if(number_of_children() < s_n_chd)
            return false;

        const Schema::schema_vector_type &s_lst = s.children();
        const Schema::schema_vector_type &lst   = children();

        for(size_t i = 0; (i < (size_t)s_n_chd) && res; i++)
        {
            res = lst[i]->compatible(*s_lst[i]);
        }
    }
    else
    {
        res = m_dtype.compatible(s.dtype());
    }
    return res;
}

//---------------------------------------------------------------------------//
bool
Schema::equals(const Schema &s) const
{
    index_t dt_id   = m_dtype.id();
    index_t s_dt_id = s.dtype().id();

    if(dt_id != s_dt_id)
        return false;
    
    bool res = true;
    
    if(dt_id == DataType::OBJECT_ID)
    {
        // all entries must be equal

        for(auto loc  = s.object_map().begin();
            loc != s.object_map().end() && res;
            loc++)
        {
            if(has_path(s.object_map().key(loc).data()))
            {
                size_t s_idx = (size_t) s.object_map().value(loc);
                res = s.children()[s_idx]->equals(child(s.object_map().key(loc).data()));
            }
            else
            {
                res = false;
            }
        }
        
        for(auto loc  = object_map().begin();
            loc != object_map().end() && res;
            loc++)
        {
          if(s.has_path(object_map().key(loc).data()))
            {
                size_t idx = (size_t) object_map().value(loc);
                res = children()[idx]->equals(s.child(object_map().key(loc).data()));
            }
            else
            {
                res = false;
            }
        }
        
    }
    else if(dt_id == DataType::LIST_ID) 
    {
        // all entries must be equal
        index_t s_n_chd = s.number_of_children();
        
        // can't be compatible in this case
        if(number_of_children() != s_n_chd)
            return false;

        const Schema::schema_vector_type &s_lst = s.children();
        const Schema::schema_vector_type &lst   = children();

        for(size_t i = 0; (i < (size_t)s_n_chd) && res; i++)
        {
            res = lst[i]->equals(*s_lst[i]);
        }
    }
    else
    {
        res = m_dtype.equals(s.dtype());
    }
    return res;
}



//-----------------------------------------------------------------------------
//
/// Transformation Methods
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
void    
Schema::compact_to(Schema &s_dest) const
{
    s_dest.reset();
    compact_to(s_dest,0);
}


//---------------------------------------------------------------------------//
std::string
Schema::to_string(const std::string &protocol,
                  index_t indent,
                  index_t depth,
                  const std::string &pad,
                  const std::string &eoe) const
{
    std::ostringstream oss;
    to_string_stream(oss,protocol,indent,depth,pad,eoe);
    return oss.str();
}

//---------------------------------------------------------------------------//
void
Schema::to_string_stream(std::ostream &os,
                         const std::string &protocol,
                         index_t indent,
                         index_t depth,
                         const std::string &pad,
                         const std::string &eoe) const
{
    if(protocol == "yaml")
    {
        to_yaml_stream(os,indent,depth,pad,eoe);
    }
    else if(protocol == "json")
    {
        to_json_stream(os,indent,depth,pad,eoe);
    }
    else
    {
        // unsupported
        CONDUIT_ERROR("<Schema::to_string_stream> "
                      "Unknown Schema::to_string protocol:" << protocol
                       <<"\nSupported protocols:\n" 
                       <<" json, yaml");
    }
}


//---------------------------------------------------------------------------//
void
Schema::to_string_stream(const std::string &stream_path,
                         const std::string &protocol,
                         index_t indent,
                         index_t depth,
                         const std::string &pad,
                         const std::string &eoe) const
{
    std::ofstream ofs;
    ofs.open(stream_path.c_str());
    if(!ofs.is_open())
    {
        CONDUIT_ERROR("<Schema::to_string_stream> failed to open file: "
                      << "\"" << stream_path << "\"");
    }
    to_string_stream(ofs,protocol,indent,depth,pad,eoe);
    ofs.close();
}

//---------------------------------------------------------------------------//
std::string
Schema::to_string_default() const
{
    return to_string();
}


//---------------------------------------------------------------------------//
std::string
Schema::to_json(index_t indent,
                index_t depth,
                const std::string &pad,
                const std::string &eoe) const
{
   std::ostringstream oss;
   to_json_stream(oss,indent,depth,pad,eoe);
   return oss.str();
}

//---------------------------------------------------------------------------//
void
Schema::to_json_stream(std::ostream &os,
                       index_t indent,
                       index_t depth,
                       const std::string &pad,
                       const std::string &eoe) const
{
    if(m_dtype.id() == DataType::OBJECT_ID)
    {
        os << eoe;
        utils::indent(os,indent,depth,pad);
        os << "{" << eoe;

        size_t nchildren = children().size();
        for(size_t i=0; i < nchildren;i++)
        {
            utils::indent(os,indent,depth+1,pad);
            os << "\""<< object_order()[i] << "\": ";
            children()[i]->to_json_stream(os,indent,depth+1,pad,eoe);
            if(i < nchildren-1)
                os << ",";
            os << eoe;
        }
        utils::indent(os,indent,depth,pad);
        os << "}";
    }
    else if(m_dtype.id() == DataType::LIST_ID)
    {
        os << eoe;
        utils::indent(os,indent,depth,pad);
        os << "[" << eoe;
        
        size_t nchildren = children().size();
        for(size_t i=0; i < nchildren;i++)
        {
            utils::indent(os,indent,depth+1,pad);
            children()[i]->to_json_stream(os,indent,depth+1,pad,eoe);
            if(i < nchildren-1)
                os << ",";
            os << eoe;
        }
        utils::indent(os,indent,depth,pad);
        os << "]";
    }
    else // assume leaf data type
    {
        m_dtype.to_json_stream(os,0,0,"","");
    }
}

//---------------------------------------------------------------------------//
void
Schema::to_json_stream(const std::string &stream_path,
                       index_t indent, 
                       index_t depth,
                       const std::string &pad,
                       const std::string &eoe) const
{
    std::ofstream ofs;
    ofs.open(stream_path.c_str());
    if(!ofs.is_open())
    {
        CONDUIT_ERROR("<Schema::to_json_stream> failed to open file: "
                      << "\"" << stream_path << "\"");
    }
    to_json_stream(ofs,indent,depth,pad,eoe);
    ofs.close();
}

//---------------------------------------------------------------------------//
std::string
Schema::to_json_default() const
{
   return to_json();
}

//---------------------------------------------------------------------------//
std::string
Schema::to_yaml(index_t indent,
                index_t depth,
                const std::string &pad,
                const std::string &eoe) const
{
   std::ostringstream oss;
   to_yaml_stream(oss,indent,depth,pad,eoe);
   return oss.str();
}

//---------------------------------------------------------------------------//
void
Schema::to_yaml_stream(std::ostream &os,
                       index_t indent,
                       index_t depth,
                       const std::string &pad,
                       const std::string &eoe) const
{
    if(m_dtype.id() == DataType::OBJECT_ID)
    {
        os << eoe;
        size_t nchildren = children().size();
        for(size_t i=0; i <  nchildren;i++)
        {
            utils::indent(os,indent,depth,pad);
            // we always need eoe
            os << object_order()[i] << ": " << eoe;
            children()[i]->to_yaml_stream(os,
                                          indent,
                                          depth+1,
                                          pad,
                                          eoe);



        }
    }
    else if(m_dtype.id() == DataType::LIST_ID)
    {
        os << eoe;
        size_t nchildren = children().size();
        for(size_t i=0; i < nchildren;i++)
        {
            utils::indent(os,indent,depth,pad);
            os << "- ";
            children()[i]->to_yaml_stream(os,
                                          indent,
                                          depth+1,
                                          pad,
                                          eoe);
        }
    }
    else // assume leaf data type
    {
        m_dtype.to_yaml_stream(os,
                               indent,
                               depth+1,
                               pad,
                               eoe);
    }
}

//---------------------------------------------------------------------------//
void
Schema::to_yaml_stream(const std::string &stream_path,
                       index_t indent, 
                       index_t depth,
                       const std::string &pad,
                       const std::string &eoe) const
{
    std::ofstream ofs;
    ofs.open(stream_path.c_str());
    if(!ofs.is_open())
    {
        CONDUIT_ERROR("<Schema::to_yaml_stream> failed to open file: "
                      << "\"" << stream_path << "\"");
    }
    to_yaml_stream(ofs,indent,depth,pad,eoe);
    ofs.close();
}

//---------------------------------------------------------------------------//
std::string
Schema::to_yaml_default() const
{
   return to_yaml();
}


//-----------------------------------------------------------------------------
//
/// Basic I/O methods
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
void
Schema::save(const std::string &ofname,
             index_t indent,
             index_t depth,
             const std::string &pad,
             const std::string &eoe) const
{
    // TODO: this is ineff, get base class rep correct?
    std::ostringstream oss;
    to_json_stream(oss,indent,depth,pad,eoe);

    std::ofstream ofile;
    ofile.open(ofname.c_str());
    if(!ofile.is_open())
    {
        CONDUIT_ERROR("<Schema::save> failed to open file: "
                      << "\"" << ofname << "\"");
    }
    ofile << oss.str();
    ofile.close();
}


//---------------------------------------------------------------------------//
void
Schema::load(const std::string &ifname)
{
    std::ifstream ifile;
    ifile.open(ifname.c_str());
    if(!ifile.is_open())
    {
        CONDUIT_ERROR("<Schema::load> failed to open file: "
                      << "\"" << ifname << "\"");
    }
    std::string res((std::istreambuf_iterator<char>(ifile)),
                     std::istreambuf_iterator<char>());
    set(res);
}



//-----------------------------------------------------------------------------
//
/// Access to children (object and list interface)
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
index_t 
Schema::number_of_children() const 
{
    if(m_dtype.id() != DataType::LIST_ID  &&
       m_dtype.id() != DataType::OBJECT_ID)
        return 0;
    return (index_t)children().size();
}



//---------------------------------------------------------------------------//
Schema &
Schema::child(index_t idx)
{
    return *children()[(size_t)idx];
}

//---------------------------------------------------------------------------//
const Schema &
Schema::child(index_t idx) const
{
    return *children()[(size_t)idx];
}


//---------------------------------------------------------------------------//
Schema *
Schema::child_ptr(index_t idx)
{
    return &child(idx);
}

//---------------------------------------------------------------------------//
const Schema *
Schema::child_ptr(index_t idx) const
{
    return &child(idx);
}


//---------------------------------------------------------------------------//
void    
Schema::remove(index_t idx)
{
    index_t dtype_id = m_dtype.id();
    if(! (dtype_id == DataType::LIST_ID || dtype_id == DataType::OBJECT_ID))
    {
        CONDUIT_ERROR("<Schema::remove> Error: Cannot remove child by index. "
                      "Schema(" << this->path() << ") "
                      "instance is not an Object or List, "
                      "and therefore "
                      "does not have children.");
    }
    
    Schema::schema_vector_type  &chldrn = children();
    if( (size_t)idx >= chldrn.size())
    {
        CONDUIT_ERROR("<Schema::remove> Invalid index:" 
                    << idx << ">=" << chldrn.size() <<  "(number_of_children)");
    }

    if(dtype_id == DataType::OBJECT_ID)
    {
        // any index above the current needs to shift down by one
        for (size_t i = (size_t)idx; i < object_order().size(); i++)
        {
            auto loc = object_map().find(object_order()[i]);
            object_map().value(loc)--;
        }
        
        object_map().erase(object_order()[(size_t)idx]);
        object_order().erase(object_order().begin() + (size_t)idx);
    }

    pointer<Schema> child = chldrn[(size_t)idx];
    deallocate<Schema>(child);
    chldrn.erase(chldrn.begin() + (size_t)idx);
}

//---------------------------------------------------------------------------//
Schema &
Schema::operator[](index_t idx)
{
    return child(idx);
}

//---------------------------------------------------------------------------//
const Schema &
Schema::operator[](index_t idx) const
{
    return child(idx);
}

//-----------------------------------------------------------------------------
//
/// Object interface methods
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
Schema&
Schema::add_child(const std::string &name)
{
    if(has_child(name))
    {
        return child(name);
    }

    init_object();

    Schema* child = allocate<Schema>(m_allocator);
    child->m_parent = this;
    children().push_back(child);
    assert(object_map().find(name) == object_map().end());
    object_map().insert(name,children().size()-1);
    object_order().emplace_back(name);
    return *children()[child_index(name)];
}


//---------------------------------------------------------------------------//
Schema&
Schema::child(const std::string &name)
{
    // only objects can have named children
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::child> Error: Cannot fetch child by name."
                      "Schema(" << this->path() << ") "
                      "instance is not an Object, and therefore "
                      " does not have named children.");
    }
    return *children()[child_index(name)];
}    

//---------------------------------------------------------------------------//
const Schema&
Schema::child(const std::string &name) const
{
    // only objects can have named children
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::child> Error: Cannot fetch child by name."
                      "Schema(" << this->path() << ") "
                      "instance is not an Object, and therefore "
                      " does not have named children.");
    }
    return *children()[child_index(name)];
}

//---------------------------------------------------------------------------//
Schema&
Schema::fetch_existing(const std::string &path)
{
    // fetch w/ path forces OBJECT_ID
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::fetch_existing> Error: Cannot fetch "
                      "existing path."
                      "Schema(" << this->path() << ") "
                      "instance is not an Object, and therefore "
                      "does not have named children.");
    }
    std::string p_curr;
    std::string p_next;
    utils::split_path(path,p_curr,p_next);

    size_t idx = (size_t) child_index(p_curr);
    
    // check for parent
    if(p_curr == "..")
    {
        if(m_parent == NULL)
        {
            CONDUIT_ERROR("Tried to fetch non-existent parent Schema.")
        }
        else
        {
            return m_parent->fetch_existing(p_next);
        }
    }
    
    if(p_next.empty())
    {
        return *children()[idx];
    }
    else
    {
        return children()[idx]->fetch_existing(p_next);
    }
}


//---------------------------------------------------------------------------//
const Schema &
Schema::fetch_existing(const std::string &path) const
{
    // fetch w/ path forces OBJECT_ID
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::fetch_existing> Error: Cannot fetch "
                      "existing path."
                      "Schema(" << this->path() << ") "
                      "instance is not an Object, and therefore "
                      "does not have named children.");
    }

    std::string p_curr;
    std::string p_next;
    utils::split_path(path,p_curr,p_next);

    // check for parent
    if(p_curr == "..")
    {
        if(m_parent != NULL) // TODO: check for erro (no parent)
           return m_parent->fetch_existing(p_next);
    }

    size_t idx = (size_t) child_index(p_curr);
    
    if(p_next.empty())
    {
        return *children()[idx];
    }
    else
    {
        return children()[idx]->fetch_existing(p_next);
    }
}


//---------------------------------------------------------------------------//
index_t
Schema::child_index(const std::string &name) const
{
    index_t res=0;

    // find p_curr with an iterator
    auto loc = object_map().find(name);

    // error if child does not exist. 
    if(loc == object_map().end())
    {
        CONDUIT_ERROR("<Schema::child_index> Error: "
                      << "Schema(" << this->path() << ") "
                      << "attempt to access invalid child named:" << name);
    }
    else
    {
        res = object_map().value(loc);
    }

    return res;
}

//---------------------------------------------------------------------------//
std::string
Schema::child_name(index_t idx) const
{
    std::string res = "";

    if(m_dtype.id() == DataType::OBJECT_ID)
    {
        const Schema::object_vector_type &obj_order = object_order();
        if( (size_t) idx < obj_order.size())
        {
            res = obj_order[(size_t)idx];
        }
    }

    return res;
}

//---------------------------------------------------------------------------//
void 
Schema::rename_child(const std::string &current_name,
                     const std::string &new_name)
{
    // make sure this schema describes an object
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::rename_child> Error: Cannot rename child. "
                      "Schema(" << this->path() << ") "
                      "instance is not an Object, and therefore "
                      "does not have named children.");
    }

    // check if current_name is valid
    if(!has_child(current_name))
    {
        CONDUIT_ERROR("<Schema::rename_child> Cannot rename child, "
                      "source child named: "
                      << "'" << current_name << "'" <<
                      " does not exist.");
    }

    // finally, make sure new_name isn't already a child
    if(has_child(new_name))
    {
        CONDUIT_ERROR("<Schema::rename_child> Cannot rename child, "
                      "destination child with name: "
                       << "'" << new_name << "'" <<
                      " already exists.");
    }

    Schema::object_map_type &obj_map = object_map();
    index_t idx = obj_map.value(obj_map.find(current_name));

    // update string to index map

    // remove current_name
    obj_map.erase(current_name);
    // link new_name to the idx
    obj_map.insert(new_name,idx);
    // update index to string lookup
    object_order()[(size_t)idx] = new_name;

    // we don't need to modify children(), we are not changing the
    // child schema 
}


//---------------------------------------------------------------------------//
Schema &
Schema::fetch(const std::string &path)
{
    // fetch w/ path forces OBJECT_ID
    init_object();
        
    std::string p_curr;
    std::string p_next;
    utils::split_path(path,p_curr,p_next);

    // handle parent 
    // check for parent
    if(p_curr == "..")
    {
        if(m_parent != NULL) // TODO: check for error (no parent)
           return m_parent->fetch(p_next);
    }
    
    if (!has_path(p_curr)) 
    {
        Schema* my_schema = allocate<Schema>(m_allocator);
        my_schema->m_parent = this;
        children().push_back(my_schema);
        object_map().insert(p_curr, children().size() - 1);
        object_order().emplace_back(p_curr.c_str(), p_curr.length());
    }

    size_t idx = (size_t) child_index(p_curr);
    if(p_next.empty())
    {
        return *children()[idx];
    }
    else
    {
        return children()[idx]->fetch(p_next);
    }

}


//---------------------------------------------------------------------------//
const Schema &
Schema::fetch(const std::string &path) const
{
    return fetch_existing(path);
}

//---------------------------------------------------------------------------//
Schema *
Schema::fetch_ptr(const std::string &path)
{
    return &fetch(path);
}

//---------------------------------------------------------------------------//
const Schema *
Schema::fetch_ptr(const std::string &path) const
{
    return &fetch(path);
}


//---------------------------------------------------------------------------//
const Schema &
Schema::operator[](const std::string &path) const
{
    return fetch_existing(path);
}

//---------------------------------------------------------------------------//
Schema &
Schema::operator[](const std::string &path)
{
    return fetch(path);
}

//---------------------------------------------------------------------------//
std::string 
Schema::name() const
{
    std::string name = "";

    const Schema *p = parent();
    if(p != NULL)
    {
        index_t idx = 0;
        index_t nchld = p->number_of_children();
        for(index_t i=0; i < nchld; i++)
        {
            if( p->child_ptr(i) == this )
            {
                idx = i;
            }
        }

        std::ostringstream oss;

        // if this schema has a parent, its parent is either an object or list
        if(p->dtype().is_object())
        {
            // use name
            std::string cld_name = p->child_name(idx);
            
            // check if name() includes "/", if so we need to escape
            bool escape = false;
            if(cld_name.find('/') != std::string::npos)
            {
                escape = true;
            }

            if(escape)
            {
                oss << "{";
            }

            oss << cld_name;

            if(escape)
            {
                oss << "}";
            }
        }
        else if(p->dtype().is_list())
        {
            // use order in the list
            oss << "[" << idx << "]";
        }

        name = oss.str();
    }

    return name;
}

//---------------------------------------------------------------------------//
std::string 
Schema::path() const
{
    std::string path = "";

    const Schema *p = parent();
    if(p != NULL)
    {
        std::ostringstream oss;

        std::string parent_path = p->path();
        if(parent_path.size() > 0)
        {
            oss << parent_path << "/";
        }

        oss << name();

        path = oss.str();
    }

    return path;
}

//---------------------------------------------------------------------------//
bool
Schema::has_child(const std::string &name) const
{
    // for the non-object case, has_path simply returns false
    if(m_dtype.id() != DataType::OBJECT_ID)
        return false;

    const Schema::object_map_type &ents = object_map();

    if(ents.find(name) == ents.end())
    {
        return false;
    }
    else
    {
        return true;
    }
}


//---------------------------------------------------------------------------//
bool           
Schema::has_path(const std::string &path) const
{
    // for the non-object case, has_path simply returns false
    if(m_dtype.id() != DataType::OBJECT_ID)
        return false;

    std::string p_input;

    // if there is a leading slash, we want to ignore
    // so check for and adjust if we have this case
    if(path.size() > 0 && path[0] == '/')
    {
        p_input = path.substr(1,path.size()-1);
    }
    else
    {
        p_input = path;
    }

    std::string p_curr;
    std::string p_next;
    utils::split_path(p_input,p_curr,p_next);

    // handle parent case (..)
    const Schema::object_map_type &ents = object_map();

    if(ents.find(p_curr) == ents.end())
    {
        return false;
    }

    if(!p_next.empty())
    {
        size_t idx = (size_t) ents.value(ents.find(p_curr));
        return children()[idx]->has_path(p_next);
    }
    else
    {
        return true;
    }
}


//---------------------------------------------------------------------------//
const Schema::object_vector_type&
Schema::child_names() const
{
    if(m_dtype.is_object())
    {
        return object_order();
    }
    else
    {
      static object_vector_type empty_child_names(m_allocator);
      return empty_child_names;
    }
}

//---------------------------------------------------------------------------//
void    
Schema::remove(const std::string &path)
{
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::remove> Error: Cannot remove path."
                      "Schema(" << this->path() << ") "
                      "instance is not an Object, and therefore "
                      " does not have named children.");
    }

    std::string p_curr;
    std::string p_next;
    utils::split_path(path,p_curr,p_next);

    if(!p_next.empty())
    {
        size_t idx = (size_t)child_index(p_curr);
        pointer<Schema> child = children()[idx];
        child->remove(p_next);
    }
    else
    {
        remove_child(p_curr);
    }    
}

//---------------------------------------------------------------------------//
void
Schema::remove_child(const std::string &name)
{
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::remove_child> Error: Cannot remove "
                      "child by name."
                      "Schema(" << this->path() << ") "
                      "instance is not an Object, and therefore "
                      " does not have named children.");
    }

    size_t idx = (size_t)child_index(name);
    pointer<Schema> child = children()[idx];
    // any index above the current needs to shift down by one
    for (size_t i = idx; i < object_order().size(); i++)
    {
        object_map().value(object_map().find(object_order()[i]))--;
    }
    object_map().erase(name);
    object_order().erase(object_order().begin() + idx);
    children().erase(children().begin() + idx);
    deallocate<Schema>(child);
}

//---------------------------------------------------------------------------//
Schema &
Schema::append()
{
    init_list();
    Schema *sch = allocate<Schema>(m_allocator);
    sch->m_parent = this;
    children().push_back(sch);
    return *sch;
}


//=============================================================================
//-----------------------------------------------------------------------------
//
//
// -- begin conduit::Schema private methods --
//
//
//-----------------------------------------------------------------------------
//=============================================================================


//-----------------------------------------------------------------------------
//
// -- private methods that help with init, memory allocation, and cleanup --
//
//-----------------------------------------------------------------------------

template <typename T, typename ...Args>
T*
Schema::allocate(Args&&... args) {
  using AT = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<T>;
  AT alloc(m_allocator);
  auto data = alloc.allocate(1);
  alloc.construct(data, std::forward<Args>(args)...);
  return metall::to_raw_pointer(data);
}


template <typename T>
void
Schema::deallocate(pointer<T> data) {
  using AT = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<T>;
  AT alloc(m_allocator);
  alloc.destroy(data);
  alloc.deallocate(data, 1);
}

//---------------------------------------------------------------------------//
void
Schema::init_defaults()
{
    m_dtype  = DataType::empty();
    m_hierarchy_data = NULL;
    m_parent = NULL;
}

//---------------------------------------------------------------------------//
void
Schema::init_object()
{
    if(dtype().id() != DataType::OBJECT_ID)
    {
        reset();
        m_dtype  = DataType::object();
        m_hierarchy_data = allocate<Schema_Object_Hierarchy>(m_allocator);
    }
}

//---------------------------------------------------------------------------//
void
Schema::init_list()
{
    if(dtype().id() != DataType::LIST_ID)
    {
        reset();
        m_dtype  = DataType::list();
        m_hierarchy_data = allocate<Schema_List_Hierarchy>(m_allocator);
    }
}


//---------------------------------------------------------------------------//
void
Schema::release()
{
    if(dtype().id() == DataType::OBJECT_ID ||
       dtype().id() == DataType::LIST_ID)
    {
        Schema::schema_vector_type &chld = children();
        for(size_t i=0; i< chld.size(); i++)
        {
          deallocate<Schema>(chld[i]);
        }
    }
    
    if(dtype().id() == DataType::OBJECT_ID)
    { 
        delete object_hierarchy();
    }
    else if(dtype().id() == DataType::LIST_ID)
    { 
        delete list_hierarchy();
    }

    m_dtype  = DataType::empty();
    m_hierarchy_data = NULL;
}



//-----------------------------------------------------------------------------
//
/// -- Private transform helpers -- 
//
//-----------------------------------------------------------------------------



//---------------------------------------------------------------------------//
void    
Schema::compact_to(Schema &s_dest, index_t curr_offset) const
{
    index_t dtype_id = m_dtype.id();
    
    if(dtype_id == DataType::OBJECT_ID )
    {
        s_dest.init_object();
        size_t nchildren = children().size();
        for(size_t i=0; i < nchildren;i++)
        {
            pointer<Schema> cld_src = children()[i];
            Schema &cld_dest = s_dest.add_child(object_order()[i]);
            cld_src->compact_to(cld_dest,curr_offset);
            curr_offset += cld_dest.total_bytes_compact();
        }
    }
    else if(dtype_id == DataType::LIST_ID)
    {
        s_dest.init_list();
        size_t nchildren = children().size();
        for(size_t i=0; i < nchildren ;i++)
        {            
            pointer<Schema> cld_src = children()[i];
            Schema &cld_dest = s_dest.append();
            cld_src->compact_to(cld_dest,curr_offset);
            curr_offset += cld_dest.total_bytes_compact();
        }
    }
    else if (dtype_id != DataType::EMPTY_ID)
    {
        // create a compact data type
        m_dtype.compact_to(s_dest.m_dtype);
        s_dest.m_dtype.set_offset(curr_offset);
    }
}




//---------------------------------------------------------------------------//
void 
Schema::walk_schema(const std::string &json_schema)
{
    Generator g(json_schema);
    g.walk(*this);
}


//-----------------------------------------------------------------------------
//
/// -- Private methods that help with access book keeping data structures. --
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------//
Schema::Schema_Object_Hierarchy *
Schema::object_hierarchy()
{
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::object_hierarchy()> Error: Cannot "
                      "access object_hierarchy. "
                      "Schema(" << this->path() << ") "
                      "instance is not an Object.");
    }
    return static_cast<Schema_Object_Hierarchy*>(metall::to_raw_pointer(m_hierarchy_data));
}

//---------------------------------------------------------------------------//
Schema::Schema_List_Hierarchy *
Schema::list_hierarchy()
{
    if(m_dtype.id() != DataType::LIST_ID)
    {
        CONDUIT_ERROR("<Schema::list_hierarchy()> Error: Cannot "
                      "access list_hierarchy. "
                      "Schema(" << this->path() << ") "
                      "instance is not a List.");
    }
    return static_cast<Schema_List_Hierarchy*>(metall::to_raw_pointer(m_hierarchy_data));
}


//---------------------------------------------------------------------------//
const Schema::Schema_Object_Hierarchy *
Schema::object_hierarchy() const 
{
    if(m_dtype.id() != DataType::OBJECT_ID)
    {
        CONDUIT_ERROR("<Schema::object_hierarchy()> Error: Cannot "
                      "access object_hierarchy. "
                      "Schema(" << this->path() << ") "
                      "instance is not an Object.");
    }
    return static_cast<Schema_Object_Hierarchy*>(metall::to_raw_pointer(m_hierarchy_data));
}

//---------------------------------------------------------------------------//
const Schema::Schema_List_Hierarchy *
Schema::list_hierarchy() const 
{    
    if(m_dtype.id() != DataType::LIST_ID)
    {
        CONDUIT_ERROR("<Schema::list_hierarchy()> Error: Cannot "
                      "access list_hierarchy. "
                      "Schema(" << this->path() << ") "
                      "instance is not a List.");
    }
    return static_cast<Schema_List_Hierarchy*>(metall::to_raw_pointer(m_hierarchy_data));
}


//---------------------------------------------------------------------------//
Schema::schema_vector_type &
Schema::children()
{
    index_t dtype_id = m_dtype.id();
    if( ! ( dtype_id == DataType::OBJECT_ID || 
            dtype_id ==  DataType::LIST_ID ))
    {
        CONDUIT_ERROR("<Schema::children()> Error: Cannot "
                      "access children. "
                      "Schema(" << this->path() << ") "
                      "instance is not an Object or List, "
                      "and therefore "
                      "does not have children.");
    }
    
    if ( dtype_id == DataType::OBJECT_ID)
    {
        return object_hierarchy()->children;
    } 
    else
    {
        return list_hierarchy()->children;
    }

}

//---------------------------------------------------------------------------//
Schema::object_map_type &
Schema::object_map()
{
    return object_hierarchy()->object_map;
}


//---------------------------------------------------------------------------//
Schema::object_vector_type &
Schema::object_order()
{
    return object_hierarchy()->object_order;
}

//---------------------------------------------------------------------------//
const Schema::schema_vector_type &
Schema::children() const
{
    if (m_dtype.id() == DataType::OBJECT_ID)
    {
        return object_hierarchy()->children;
    } 
    else 
    {
        return list_hierarchy()->children;
    }
}

//---------------------------------------------------------------------------//
index_t
Schema::child_index(Schema *schema_ptr) const
{
    // find the index of schema_ptr
    index_t nchld = number_of_children();
    for(index_t i=0; i < nchld; i++)
    {
        if(child_ptr(i) == schema_ptr )
        {
            return i;
        }
    }
    return -1;
}

//---------------------------------------------------------------------------//
const Schema::object_map_type &
Schema::object_map() const
{
    return object_hierarchy()->object_map;
}


//---------------------------------------------------------------------------//
const Schema::object_vector_type &
Schema::object_order() const
{
    return object_hierarchy()->object_order;
}




//---------------------------------------------------------------------------//
void
Schema::object_map_print() const
{
    size_t sz = object_order().size();
    for(size_t i=0;i<sz;i++)
    {
        std::cout << object_order()[i] << " ";
    }
    std::cout << std::endl;
}


//---------------------------------------------------------------------------//
void
Schema::object_order_print() const
{

    for(auto loc = object_map().begin(); loc != object_map().end();loc++)
    {
       std::cout << object_map().key(loc) << ":" << object_map().value(loc) << " ";
    }
    std::cout << std::endl;
}


}
//-----------------------------------------------------------------------------
// -- end conduit:: --
//-----------------------------------------------------------------------------

