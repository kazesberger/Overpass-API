#include "../../template_db/block_backend.h"
#include "../../template_db/random_file.h"
#include "../core/settings.h"
#include "area_query.h"
#include "bbox_query.h"
#include "meta_collector.h"
#include "newer.h"
#include "query.h"
#include "user.h"

#include <algorithm>
#include <sstream>

#include <iostream>

using namespace std;

//-----------------------------------------------------------------------------

class Item_Constraint : public Query_Constraint
{
  public:
    Item_Constraint(Item_Statement& item_) : item(&item_) {}
    void filter(Resource_Manager& rman, Set& into);
    virtual ~Item_Constraint() {}
    
  private:
    Item_Statement* item;
};

template< typename TIndex, typename TObject >
void item_filter_map
    (map< TIndex, vector< TObject > >& modify,
     const map< TIndex, vector< TObject > >& read)
{
  for (typename map< TIndex, vector< TObject > >::iterator it = modify.begin();
      it != modify.end(); ++it)
  {
    sort(it->second.begin(), it->second.end());
    typename map< TIndex, vector< TObject > >::const_iterator
        from_it = read.find(it->first);
    if (from_it == read.end())
    {
      it->second.clear();
      continue;
    }
    vector< TObject > local_into;
    for (typename vector< TObject >::const_iterator iit = from_it->second.begin();
        iit != from_it->second.end(); ++iit)
    {
      if (binary_search(it->second.begin(), it->second.end(), *iit))
	local_into.push_back(*iit);
    }
    it->second.swap(local_into);
  }
}

void Item_Constraint::filter(Resource_Manager& rman, Set& into)
{
  item_filter_map(into.nodes, rman.sets()[item->get_result_name()].nodes);
  item_filter_map(into.ways, rman.sets()[item->get_result_name()].ways);
  item_filter_map(into.relations, rman.sets()[item->get_result_name()].relations);
}

//-----------------------------------------------------------------------------

class Newer_Constraint : public Query_Constraint
{
  public:
    Newer_Constraint(Newer_Statement& newer) : timestamp(newer.get_timestamp()) {}
    void filter(Resource_Manager& rman, Set& into);
    virtual ~Newer_Constraint() {}
    
  private:
    uint64 timestamp;
};

template< typename TIndex, typename TObject >
void newer_filter_map
    (map< TIndex, vector< TObject > >& modify,
     Resource_Manager& rman, uint64 timestamp, File_Properties* file_properties)
{
  if (modify.empty())
    return;
  Meta_Collector< TIndex, TObject > meta_collector
      (modify, *rman.get_transaction(), file_properties, false);
  for (typename map< TIndex, vector< TObject > >::iterator it = modify.begin();
      it != modify.end(); ++it)
  {
    vector< TObject > local_into;
    for (typename vector< TObject >::const_iterator iit = it->second.begin();
        iit != it->second.end(); ++iit)
    {
      const OSM_Element_Metadata_Skeleton* meta_skel
	  = meta_collector.get(it->first, iit->id);
      if ((meta_skel) && (meta_skel->timestamp >= timestamp))
	local_into.push_back(*iit);
    }
    it->second.swap(local_into);
  }
}

void Newer_Constraint::filter(Resource_Manager& rman, Set& into)
{
  newer_filter_map(into.nodes, rman, timestamp, meta_settings().NODES_META);
  newer_filter_map(into.ways, rman, timestamp, meta_settings().WAYS_META);
  newer_filter_map(into.relations, rman, timestamp, meta_settings().RELATIONS_META);
}

//-----------------------------------------------------------------------------

class User_Constraint : public Query_Constraint
{
  public:
    User_Constraint(User_Statement& user_) : user(&user_) {}
    void filter(Resource_Manager& rman, Set& into);
    virtual ~User_Constraint() {}
    
  private:
    User_Statement* user;
};

template< typename TIndex, typename TObject >
void user_filter_map
    (map< TIndex, vector< TObject > >& modify,
     Resource_Manager& rman, uint32 user_id, File_Properties* file_properties)
{
  if (modify.empty())
    return;
  Meta_Collector< TIndex, TObject > meta_collector
      (modify, *rman.get_transaction(), file_properties, false);
  for (typename map< TIndex, vector< TObject > >::iterator it = modify.begin();
      it != modify.end(); ++it)
  {
    vector< TObject > local_into;
    for (typename vector< TObject >::const_iterator iit = it->second.begin();
        iit != it->second.end(); ++iit)
    {
      const OSM_Element_Metadata_Skeleton* meta_skel
	  = meta_collector.get(it->first, iit->id);
      if ((meta_skel) && (meta_skel->user_id == user_id))
	local_into.push_back(*iit);
    }
    it->second.swap(local_into);
  }
}

void User_Constraint::filter(Resource_Manager& rman, Set& into)
{
  uint32 user_id = user->get_id(*rman.get_transaction());
  user_filter_map(into.nodes, rman, user_id, meta_settings().NODES_META);
  user_filter_map(into.ways, rman, user_id, meta_settings().WAYS_META);
  user_filter_map(into.relations, rman, user_id, meta_settings().RELATIONS_META);
}

//-----------------------------------------------------------------------------

const unsigned int QUERY_NODE = 1;
const unsigned int QUERY_WAY = 2;
const unsigned int QUERY_RELATION = 3;
// const unsigned int QUERY_AREA = 4;

Query_Statement::~Query_Statement()
{
  for (vector< Query_Constraint* >::iterator it = constraints.begin();
      it != constraints.end(); ++it)
    delete *it;
}

void Query_Statement::set_attributes(const char **attr)
{
  map< string, string > attributes;
  
  attributes["into"] = "_";
  attributes["type"] = "";
  
  eval_cstr_array(get_name(), attributes, attr);
  
  output = attributes["into"];
  if (attributes["type"] == "node")
    type = QUERY_NODE;
  else if (attributes["type"] == "way")
    type = QUERY_WAY;
  else if (attributes["type"] == "relation")
    type = QUERY_RELATION;
/*  else if (attributes["type"] == "area")
    type = QUERY_AREA;*/
  else
  {
    type = 0;
    ostringstream temp;
    temp<<"For the attribute \"type\" of the element \"query\""
	<<" the only allowed values are \"node\", \"way\" or \"relation\".";
/*    temp<<"For the attribute \"type\" of the element \"query\""
	<<" the only allowed values are \"node\", \"way\", \"relation\" or \"area\".";*/
    add_static_error(temp.str());
  }
}

void Query_Statement::add_statement(Statement* statement, string text)
{
  assure_no_text(text, this->get_name());
  
  Has_Kv_Statement* has_kv(dynamic_cast<Has_Kv_Statement*>(statement));
  if (has_kv)
  {
    key_values.push_back(make_pair< string, string >
	(has_kv->get_key(), has_kv->get_value()));
    return;
  }
  Area_Query_Statement* area(dynamic_cast<Area_Query_Statement*>(statement));
  Around_Statement* around(dynamic_cast<Around_Statement*>(statement));
  Bbox_Query_Statement* bbox(dynamic_cast<Bbox_Query_Statement*>(statement));
  Item_Statement* item(dynamic_cast<Item_Statement*>(statement));
  Newer_Statement* newer(dynamic_cast<Newer_Statement*>(statement));
  User_Statement* user(dynamic_cast<User_Statement*>(statement));
  if (area != 0)
  {
    if (type != QUERY_NODE)
    {
      ostringstream temp;
      temp<<"An area-query as substatement is only allowed for queries of type \"node\".";
      add_static_error(temp.str());
      return;
    }
    if ((area_restriction != 0) || (around_restriction != 0) || (bbox_restriction != 0) ||
        (item_restriction != 0))
    {
      ostringstream temp;
      temp<<"A query statement may contain at most one area-query, around-query, bbox-query, "
	  <<"or item as substatement.";
      add_static_error(temp.str());
      return;
    }
    area_restriction = area;
    return;
  }
  else if (around != 0)
  {
    if (type != QUERY_NODE)
    {
      ostringstream temp;
      temp<<"An around as substatement is only allowed for queries of type \"node\".";
      add_static_error(temp.str());
      return;
    }
    if ((area_restriction != 0) || (around_restriction != 0) || (bbox_restriction != 0) ||
        (item_restriction != 0))
    {
      ostringstream temp;
      temp<<"A query statement may contain at most one area-query, around-query, bbox-query, "
	  <<"or item as substatement.";
      add_static_error(temp.str());
      return;
    }
    around_restriction = around;
    return;
  }
  else if (bbox != 0)
  {
    if (type != QUERY_NODE)
    {
      ostringstream temp;
      temp<<"A bbox-query as substatement is only allowed for queries of type \"node\".";
      add_static_error(temp.str());
      return;
    }
    if ((area_restriction != 0) || (around_restriction != 0) || (bbox_restriction != 0) ||
        (item_restriction != 0))
    {
      ostringstream temp;
      temp<<"A query statement may contain at most one area-query, around-query, bbox-query, "
	  <<"or item as substatement.";
      add_static_error(temp.str());
      return;
    }
    bbox_restriction = bbox;
    return;
  }
  else if (item != 0)
  {
    if ((area_restriction != 0) || (around_restriction != 0) || (bbox_restriction != 0) ||
        (item_restriction != 0))
      constraints.push_back(new Item_Constraint(*item));
    else
      item_restriction = item;
  }
  else if (user != 0)
    constraints.push_back(new User_Constraint(*user));
  else if (newer != 0)
    constraints.push_back(new Newer_Constraint(*newer));
  else
    substatement_error(get_name(), statement);
}

void Query_Statement::forecast()
{
/*  Set_Forecast& sf_out(declare_write_set(output));
    
  if (type == QUERY_NODE)
  {
    map< uint, pair< string, string > > key_value_counts;
    for (vector< pair< string, string > >::const_iterator it(key_values.begin());
	 it != key_values.end(); ++it)
    {
      uint count(node_kv_to_count_query(it->first, it->second));
      key_value_counts.insert
	  (make_pair< uint, pair< string, string > >(count, *it));
    }
    uint i(0);
    bool reordered(false);
    for (map< uint, pair< string, string > >::const_iterator it(key_value_counts.begin());
	 it != key_value_counts.end(); ++it)
    {
      reordered |= (it->second != key_values[i]);
      key_values[i++] = it->second;
    }
    if (reordered)
    {
      ostringstream temp;
      temp<<"The clauses of this query have been reordered to improve performance:<br/>\n";
      for (map< uint32, pair< string, string > >::const_iterator it(key_value_counts.begin()); ; )
      {
	temp<<"Has_Kv \""<<it->second.first<<"\" \""<<it->second.second<<"\": "<<it->first
	    <<" results expected.";
	if (++it != key_value_counts.end())
	  temp<<"<br/>\n";
	else
	  break;
      }
      add_sanity_remark(temp.str());
    }
    
    if (key_value_counts.empty())
    {
      sf_out.node_count = 400*1000*1000;
      add_sanity_error("A query with empty conditions is not allowed.");
    }
    else
      sf_out.node_count = key_value_counts.begin()->first;
    declare_used_time(24000 + sf_out.node_count);
  }
  else if (type == QUERY_WAY)
  {
    map< uint, pair< string, string > > key_value_counts;
    for (vector< pair< string, string > >::const_iterator it(key_values.begin());
	 it != key_values.end(); ++it)
    {
      uint count(way_kv_to_count_query(it->first, it->second));
      key_value_counts.insert
	  (make_pair< uint, pair< string, string > >(count, *it));
    }
    uint i(0);
    bool reordered(false);
    for (map< uint, pair< string, string > >::const_iterator it(key_value_counts.begin());
	 it != key_value_counts.end(); ++it)
    {
      reordered |= (it->second != key_values[i]);
      key_values[i++] = it->second;
    }
    if (reordered)
    {
      ostringstream temp;
      temp<<"The clauses of this query have been reordered to improve performance:<br/>\n";
      for (map< uint32, pair< string, string > >::const_iterator it(key_value_counts.begin()); ; )
      {
	temp<<"Has_Kv \""<<it->second.first<<"\" \""<<it->second.second<<"\": "<<it->first
	    <<" results expected.";
	if (++it != key_value_counts.end())
	  temp<<"<br/>\n";
	else
	  break;
      }
      add_sanity_remark(temp.str());
    }
    
    if (key_value_counts.empty())
    {
      sf_out.way_count = 30*1000*1000;
      add_sanity_error("A query with empty conditions is not allowed.");
    }
    else
      sf_out.way_count = key_value_counts.begin()->first;
    
    declare_used_time(90000 + sf_out.way_count);
  }
  else if (type == QUERY_RELATION)
  {
    map< uint, pair< string, string > > key_value_counts;
    for (vector< pair< string, string > >::const_iterator it(key_values.begin());
	 it != key_values.end(); ++it)
    {
      uint count(relation_kv_to_count_query(it->first, it->second));
      key_value_counts.insert
	  (make_pair< uint, pair< string, string > >(count, *it));
    }
    uint i(0);
    bool reordered(false);
    for (map< uint, pair< string, string > >::const_iterator it(key_value_counts.begin());
	 it != key_value_counts.end(); ++it)
    {
      reordered |= (it->second != key_values[i]);
      key_values[i++] = it->second;
    }
    if (reordered)
    {
      ostringstream temp;
      temp<<"The clauses of this query have been reordered to improve performance:<br/>\n";
      for (map< uint32, pair< string, string > >::const_iterator it(key_value_counts.begin()); ; )
      {
	temp<<"Has_Kv \""<<it->second.first<<"\" \""<<it->second.second<<"\": "<<it->first
	    <<" results expected.";
	if (++it != key_value_counts.end())
	  temp<<"<br/>\n";
	else
	  break;
      }
      add_sanity_remark(temp.str());
    }
    
    if (key_value_counts.empty())
    {
      sf_out.relation_count = 100*1000;
      add_sanity_error("A query with empty conditions is not allowed.");
    }
    else
      sf_out.relation_count = key_value_counts.begin()->first;
    declare_used_time(100 + sf_out.relation_count);
  }
  else if (type == QUERY_AREA)
  {
    if (key_value_counts.empty())
    {
      sf_out.area_count = 100*1000;
      add_sanity_error("A query with empty conditions is not allowed.");
    }
    else
      sf_out.area_count = 15;
    declare_used_time(30*1000);
  }
  finish_statement_forecast();
    
  display_full();
  display_state();*/
}

vector< uint32 >* Query_Statement::collect_ids
  (const vector< pair< string, string > >& key_values,
   const File_Properties& file_prop, uint32 stopwatch_account,
   Resource_Manager& rman)
{
  if (key_values.empty())
    return new vector< uint32 >();
 
  stopwatch.stop(Stopwatch::NO_DISK);
  Block_Backend< Tag_Index_Global, Uint32_Index > tags_db
      (rman.get_transaction()->data_index(&file_prop));
  
  vector< uint32 >* new_ids(new vector< uint32 >());
  vector< pair< string, string > >::const_iterator it = key_values.begin();
  if (it->second != "")
  {
    set< Tag_Index_Global > tag_req;
    Tag_Index_Global idx;
    idx.key = it->first;
    idx.value = it->second;
    tag_req.insert(idx);
    for (Block_Backend< Tag_Index_Global, Uint32_Index >::Discrete_Iterator
        it2(tags_db.discrete_begin(tag_req.begin(), tag_req.end()));
        !(it2 == tags_db.discrete_end()); ++it2)
      new_ids->push_back(it2.object().val());
  }
  else
  {
    set< pair< Tag_Index_Global, Tag_Index_Global > > range_req;
    pair< Tag_Index_Global, Tag_Index_Global > idx_pair;
    idx_pair.first.key = it->first;
    idx_pair.first.value = "";
    idx_pair.second.key = it->first + (char)0;
    idx_pair.second.value = "";
    range_req.insert(idx_pair);
    for (Block_Backend< Tag_Index_Global, Uint32_Index >::Range_Iterator
        it2(tags_db.range_begin
          (Default_Range_Iterator< Tag_Index_Global >(range_req.begin()),
	   Default_Range_Iterator< Tag_Index_Global >(range_req.end())));
        !(it2 == tags_db.range_end()); ++it2)
      new_ids->push_back(it2.object().val());
  }
  stopwatch.add(stopwatch_account, tags_db.read_count());
  stopwatch.stop(stopwatch_account);
  sort(new_ids->begin(), new_ids->end());
  stopwatch.stop(Stopwatch::NO_DISK);
  ++it;
  
  for (; it != key_values.end(); ++it)
  {
    vector< uint32 >* old_ids = new_ids;
    new_ids = new vector< uint32 >();
    if (it->second != "")
    {
      set< Tag_Index_Global > tag_req;
      Tag_Index_Global idx;
      idx.key = it->first;
      idx.value = it->second;
      tag_req.insert(idx);
      for (Block_Backend< Tag_Index_Global, Uint32_Index >::Discrete_Iterator
          it2(tags_db.discrete_begin(tag_req.begin(), tag_req.end()));
          !(it2 == tags_db.discrete_end()); ++it2)
      {
        if (binary_search(old_ids->begin(), old_ids->end(), it2.object().val()))
	  new_ids->push_back(it2.object().val());
      }
    }
    else
    {
      set< pair< Tag_Index_Global, Tag_Index_Global > > range_req;
      pair< Tag_Index_Global, Tag_Index_Global > idx_pair;
      idx_pair.first.key = it->first;
      idx_pair.first.value = "";
      idx_pair.second.key = it->first + (char)0;
      idx_pair.second.value = "";
      range_req.insert(idx_pair);
      for (Block_Backend< Tag_Index_Global, Uint32_Index >::Range_Iterator
	  it2(tags_db.range_begin
	  (Default_Range_Iterator< Tag_Index_Global >(range_req.begin()),
	   Default_Range_Iterator< Tag_Index_Global >(range_req.end())));
	  !(it2 == tags_db.range_end()); ++it2)
      {
	if (binary_search(old_ids->begin(), old_ids->end(), it2.object().val()))
	  new_ids->push_back(it2.object().val());
      }
    }
    delete(old_ids);
    stopwatch.add(stopwatch_account, tags_db.read_count());
    stopwatch.stop(stopwatch_account);
    sort(new_ids->begin(), new_ids->end());
    stopwatch.stop(Stopwatch::NO_DISK);
    
    rman.health_check(*this);
  }
  
  return new_ids;
}

template < typename TIndex, typename TObject >
void Query_Statement::get_elements_by_id_from_db
    (map< TIndex, vector< TObject > >& elements,
     const vector< uint32 >& ids, const set< pair< TIndex, TIndex > >& range_req,
     Resource_Manager& rman, File_Properties& file_prop)
{
  uint elements_count = 0;
  Block_Backend< TIndex, TObject > elements_db
      (rman.get_transaction()->data_index(&file_prop));
  for (typename Block_Backend< TIndex, TObject >::Range_Iterator
      it(elements_db.range_begin
          (Default_Range_Iterator< TIndex >(range_req.begin()),
	   Default_Range_Iterator< TIndex >(range_req.end())));
      !(it == elements_db.range_end()); ++it)
  {
    if (++elements_count >= 64*1024)
    {
      elements_count = 0;
      rman.health_check(*this);
    }
    
    if (binary_search(ids.begin(), ids.end(), it.object().id))
      elements[it.index()].push_back(it.object());
  }    
}

void Query_Statement::execute(Resource_Manager& rman)
{
  if (key_values.empty() && (!item_restriction))
    return;

  stopwatch.start();
  Set into;
  
  if (type == QUERY_NODE)
  {
    vector< uint32 >* ids(collect_ids
        (key_values, *osm_base_settings().NODE_TAGS_GLOBAL,
         Stopwatch::NODE_TAGS_GLOBAL, rman));

    set< pair< Uint32_Index, Uint32_Index > > nodes_req;
    set< Uint31_Index > area_blocks_req;	
    set< Uint32_Index > obj_req;
    set< pair< Uint32_Index, Uint32_Index > > range_req;
    if (area_restriction != 0)
    {
      stopwatch.stop(Stopwatch::NO_DISK);
      area_restriction->get_ranges(nodes_req, area_blocks_req, rman);
      stopwatch.stop(Stopwatch::AREAS);
      for (set< pair< Uint32_Index, Uint32_Index > >::const_iterator
	  it(nodes_req.begin()); it != nodes_req.end(); ++it)
      {
	for (uint32 i(it->first.val()); i < it->second.val(); ++i)
	  obj_req.insert(Uint32_Index(i));
      }
    }
    else if (around_restriction != 0)
    {
      set< pair< Uint32_Index, Uint32_Index > > ranges
          (around_restriction->calc_ranges
	      (rman.sets()[around_restriction->get_source_name()].nodes));
      for (set< pair< Uint32_Index, Uint32_Index > >::const_iterator
	it(ranges.begin()); it != ranges.end(); ++it)
      {
	pair< Uint32_Index, Uint32_Index > range(make_pair(it->first, it->second));
	range_req.insert(range);
      }
    }
    else if (bbox_restriction != 0)
    {
      vector< pair< uint32, uint32 > >* ranges(bbox_restriction->calc_ranges());
      for (vector< pair< uint32, uint32 > >::const_iterator
	it(ranges->begin()); it != ranges->end(); ++it)
      {
	pair< Uint32_Index, Uint32_Index > range
	    (make_pair(Uint32_Index(it->first), Uint32_Index(it->second)));
	range_req.insert(range);
      }
      delete(ranges);
    }
    else
    {
      Random_File< Uint32_Index > random
          (rman.get_transaction()->random_index(osm_base_settings().NODES));
      for (vector< uint32 >::const_iterator it(ids->begin());
          it != ids->end(); ++it)
        range_req.insert(make_pair(random.get(*it), Uint32_Index(random.get(*it).val()+1)));
    }
    
    stopwatch.stop(Stopwatch::NO_DISK);
    if (area_restriction != 0)
    {
      area_restriction->collect_nodes
          (nodes_req, area_blocks_req, ids, into.nodes, stopwatch, rman);
      stopwatch.stop(Stopwatch::NO_DISK);
    }
    else if (around_restriction != 0)
    {
      uint nodes_count = 0;
      Block_Backend< Uint32_Index, Node_Skeleton > nodes_db
	  (rman.get_transaction()->data_index(osm_base_settings().NODES));
      for (Block_Backend< Uint32_Index, Node_Skeleton >::Range_Iterator
          it(nodes_db.range_begin
             (Default_Range_Iterator< Uint32_Index >(range_req.begin()),
	      Default_Range_Iterator< Uint32_Index >(range_req.end())));
          !(it == nodes_db.range_end()); ++it)
      {
	if (++nodes_count >= 64*1024)
	{
	  nodes_count = 0;
	  rman.health_check(*this);
	}
	
	if (binary_search(ids->begin(), ids->end(), it.object().id))
	{
	  double lat(Node::lat(it.index().val(), it.object().ll_lower));
	  double lon(Node::lon(it.index().val(), it.object().ll_lower));
	  if (around_restriction->is_inside(lat, lon))
	    into.nodes[it.index()].push_back(it.object());
	}
      }
      stopwatch.add(Stopwatch::NODES, nodes_db.read_count());
      stopwatch.stop(Stopwatch::NODES);
    }
    else if (bbox_restriction != 0)
    {
      uint nodes_count = 0;
      Block_Backend< Uint32_Index, Node_Skeleton > nodes_db
	  (rman.get_transaction()->data_index(osm_base_settings().NODES));
      for (Block_Backend< Uint32_Index, Node_Skeleton >::Range_Iterator
          it(nodes_db.range_begin
             (Default_Range_Iterator< Uint32_Index >(range_req.begin()),
	      Default_Range_Iterator< Uint32_Index >(range_req.end())));
          !(it == nodes_db.range_end()); ++it)
      {
	if (++nodes_count >= 64*1024)
	{
	  nodes_count = 0;
	  rman.health_check(*this);
	}
	
	if (binary_search(ids->begin(), ids->end(), it.object().id))
	{
	  double lat(Node::lat(it.index().val(), it.object().ll_lower));
	  double lon(Node::lon(it.index().val(), it.object().ll_lower));
	  if ((lat >= bbox_restriction->get_south()) &&
	      (lat <= bbox_restriction->get_north()) &&
	      (((lon >= bbox_restriction->get_west()) &&
	       (lon <= bbox_restriction->get_east())) ||
	       ((bbox_restriction->get_east() < bbox_restriction->get_west()) &&
	        ((lon >= bbox_restriction->get_west()) ||
		 (lon <= bbox_restriction->get_east())))))
	    into.nodes[it.index()].push_back(it.object());
	}
      }
      stopwatch.add(Stopwatch::NODES, nodes_db.read_count());
      stopwatch.stop(Stopwatch::NODES);
    }
    else if (item_restriction)
    {
      map< Uint32_Index, vector< Node_Skeleton > >& from
          (rman.sets()[item_restriction->get_result_name()].nodes);
      
      for (map< Uint32_Index, vector< Node_Skeleton > >::const_iterator iit = from.begin();
          iit != from.end(); ++iit)
      {
	for (vector< Node_Skeleton >::const_iterator cit = iit->second.begin();
	    cit != iit->second.end(); ++cit)
	{
	  if ((binary_search(ids->begin(), ids->end(), cit->id)) || (ids->empty()))
	    into.nodes[iit->first].push_back(*cit);
	}
      }
    }
    else
    {
      get_elements_by_id_from_db< Uint32_Index, Node_Skeleton >
          (into.nodes, *ids, range_req, rman, *osm_base_settings().NODES);
    }
  }
  else if (type == QUERY_WAY)
  {
    vector< uint32 >* ids(collect_ids
        (key_values, *osm_base_settings().WAY_TAGS_GLOBAL,
	 Stopwatch::WAY_TAGS_GLOBAL, rman));

    if (item_restriction)
    {
      map< Uint31_Index, vector< Way_Skeleton > >& from
          (rman.sets()[item_restriction->get_result_name()].ways);
      
      for (map< Uint31_Index, vector< Way_Skeleton > >::const_iterator iit = from.begin();
          iit != from.end(); ++iit)
      {
	for (vector< Way_Skeleton >::const_iterator cit = iit->second.begin();
	    cit != iit->second.end(); ++cit)
	{
	  if ((binary_search(ids->begin(), ids->end(), cit->id)) || (ids->empty()))
	    into.ways[iit->first].push_back(*cit);
	}
      }
    }
    else
    {
      set< pair< Uint31_Index, Uint31_Index > > range_req;
      {
        Random_File< Uint31_Index > random
            (rman.get_transaction()->random_index(osm_base_settings().WAYS));
        for (vector< uint32 >::const_iterator it(ids->begin());
            it != ids->end(); ++it)
          range_req.insert(make_pair(random.get(*it), Uint31_Index(random.get(*it).val()+1)));
      }
      
      get_elements_by_id_from_db< Uint31_Index, Way_Skeleton >
          (into.ways, *ids, range_req, rman, *osm_base_settings().WAYS);
    }
  }
  else if (type == QUERY_RELATION)
  {
    vector< uint32 >* ids(collect_ids
        (key_values, *osm_base_settings().RELATION_TAGS_GLOBAL,
	 Stopwatch::RELATION_TAGS_GLOBAL, rman));
    
    if (item_restriction)
    {
      map< Uint31_Index, vector< Relation_Skeleton > >& from
          (rman.sets()[item_restriction->get_result_name()].relations);
      
      for (map< Uint31_Index, vector< Relation_Skeleton > >::const_iterator iit = from.begin();
          iit != from.end(); ++iit)
      {
	for (vector< Relation_Skeleton >::const_iterator cit = iit->second.begin();
	    cit != iit->second.end(); ++cit)
	{
	  if ((binary_search(ids->begin(), ids->end(), cit->id)) || (ids->empty()))
	    into.relations[iit->first].push_back(*cit);
	}
      }
    }
    else
    {
      set< pair< Uint31_Index, Uint31_Index > > range_req;
      {
        Random_File< Uint31_Index > random
            (rman.get_transaction()->random_index(osm_base_settings().RELATIONS));
        for (vector< uint32 >::const_iterator it(ids->begin());
            it != ids->end(); ++it)
          range_req.insert(make_pair(random.get(*it), Uint31_Index(random.get(*it).val()+1)));
      }
    
      get_elements_by_id_from_db< Uint31_Index, Relation_Skeleton >
          (into.relations, *ids, range_req, rman, *osm_base_settings().RELATIONS);
    }
  }
  
  for (vector< Query_Constraint* >::iterator it = constraints.begin();
      it != constraints.end(); ++it)
    (*it)->filter(rman, into);
  
  into.nodes.swap(rman.sets()[output].nodes);
  into.ways.swap(rman.sets()[output].ways);
  into.relations.swap(rman.sets()[output].relations);
  rman.sets()[output].areas.clear();
  
  stopwatch.report(get_name());  
  rman.health_check(*this);
}

//-----------------------------------------------------------------------------

void Has_Kv_Statement::set_attributes(const char **attr)
{
  map< string, string > attributes;
  
  attributes["k"] = "";
  attributes["v"] = "";
  
  eval_cstr_array(get_name(), attributes, attr);
  
  key = attributes["k"];
  value = attributes["v"];
  if (key == "")
  {
    ostringstream temp("");
    temp<<"For the attribute \"k\" of the element \"has-kv\""
	<<" the only allowed values are non-empty strings.";
    add_static_error(temp.str());
  }
}

void Has_Kv_Statement::forecast()
{
  // will never be called
}
