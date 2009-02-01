#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <math.h>
#include <stdlib.h>
#include <vector>
#include "expat_justparse_interface.h"
#include "script_datatypes.h"
#include "script_queries.h"
#include "script_tools.h"
#include "user_interface.h"
#include "make_area_statement.h"

#include <mysql.h>

using namespace std;

void Make_Area_Statement::set_attributes(const char **attr)
{
  map< string, string > attributes;
  
  attributes["from"] = "_";
  attributes["into"] = "_";
  attributes["pivot"] = "";
  
  eval_cstr_array(get_name(), attributes, attr);
  
  input = attributes["from"];
  output = attributes["into"];
  tags = attributes["pivot"];
}

inline void area_insert(Area& area, int lat1, int lon1, int lat2, int lon2)
{
  if (lon1 < lon2)
  {
    unsigned int div((lon2 - lon1)/100000+1);
    for (unsigned int i(0); i < div; ++i)
      area.segments.insert(Line_Segment
	  (lat1+((long long)(lat2-lat1))*i/div, lon1+((long long)(lon2-lon1))*i/div,
	   lat1+((long long)(lat2-lat1))*(i+1)/div, lon1+((long long)(lon2-lon1))*(i+1)/div));
  }
  else
  {
    unsigned int div((lon1 - lon2)/100000+1);
    for (unsigned int i(0); i < div; ++i)
    {
      area.segments.insert(Line_Segment
	  (lat2+((long long)(lat1-lat2))*i/div, lon2+((long long)(lon1-lon2))*i/div,
	   lat2+((long long)(lat1-lat2))*(i+1)/div, lon2+((long long)(lon1-lon2))*(i+1)/div));
    }
  }
}

void Make_Area_Statement::execute(MYSQL* mysql, map< string, Set >& maps)
{
  set< Node > nodes;
  set< Way > ways;
  set< Relation > relations;
  set< Area > areas;
  if (input == output)
  {
    nodes = maps[output].get_nodes();
    ways = maps[output].get_ways();
    relations = maps[output].get_relations();
    areas = maps[output].get_areas();
  }
  
  map< string, Set >::const_iterator mit(maps.find(input));
  if (mit == maps.end())
  {
    maps[output] = Set(nodes, ways, relations, areas);
    return;
  }
  const set< Node >& in_nodes(mit->second.get_nodes());
  const set< Way >& in_ways(mit->second.get_ways());
  
  mit = maps.find(tags);
  int pivot_id(0), pivot_type(0);
  if (mit != maps.end())
  {
    if (mit->second.get_nodes().begin() != mit->second.get_nodes().end())
    {
      pivot_id = (mit->second.get_nodes().begin())->id;
      pivot_type = NODE;
    }
    else if (mit->second.get_ways().begin() != mit->second.get_ways().end())
    {
      pivot_id = (mit->second.get_ways().begin())->id;
      pivot_type = WAY;
    }
    else if (mit->second.get_relations().begin() != mit->second.get_relations().end())
    {
      pivot_id = (mit->second.get_relations().begin())->id;
      pivot_type = RELATION;
    }
  }
  
  ostringstream temp;
  temp<<"select id from areas where pivot = "<<pivot_id
      <<" and pivot_type = "<<pivot_type;
  int previous_area(int_query(mysql, temp.str()));
  if (previous_area)
  {
    set< int > previous_ways;
    temp.str("");
    temp<<"select way from area_ways where id = "<<previous_area;
    multiint_query(mysql, temp.str(), previous_ways);
    
    set< Way >::const_iterator it(in_ways.begin());
    set< int >::const_iterator it2(previous_ways.begin());
    while ((it != in_ways.end()) && (it2 != previous_ways.end()) && ((it++)->id == *(it2++)))
      ;
    if ((it == in_ways.end()) && (it2 == previous_ways.end()))
    {
      temp.str("");
      temp<<"Make-Area: The pivot "<<types_lowercase[pivot_type]<<' '<<pivot_id
	  <<" is already referred by area "<<previous_area<<" made with the same set of ways.\n";
      runtime_remark(temp.str(), cout);
      
      ostringstream stack;
      for (vector< pair< int, int > >::const_iterator it(get_stack().begin());
	   it != get_stack().end(); ++it)
	stack<<it->first<<' '<<it->second<<' ';
      temp.str("");
      temp<<"insert area_origins values ("<<previous_area<<", "
	  <<get_rule_id()<<", "
	  <<this->get_line_number()<<", '"
	  <<stack.str()<<"')";
      mysql_query(mysql, temp.str().c_str());
      
      Area area(previous_area);
      areas.insert(area);
      maps[output] = Set(nodes, ways, relations, areas);
      return;
    }
    
    temp.str("");
    temp<<"Make-Area: The pivot "<<types_lowercase[pivot_type]<<' '<<pivot_id
	<<" is already referred by area "<<previous_area<<" made from a different set of ways.\n";
    runtime_error(temp.str(), cout);
    maps[output] = Set(nodes, ways, relations, areas);
    return;
  }
  
  Area area(int_query(mysql, "select max(id) from areas")+1);
  
  vector< set< int > > lat_intersections(179);
  set< int > node_parity_control;
  for (set< Way >::const_iterator it(in_ways.begin());
       it != in_ways.end(); ++it)
  {
    set< Node >::const_iterator onit(in_nodes.end());
    vector< int >::const_iterator iit(it->members.begin());
    if (iit != it->members.end())
    {
      if (node_parity_control.find(*iit) != node_parity_control.end())
	node_parity_control.erase(*iit);
      else
	node_parity_control.insert(*iit);
      onit = in_nodes.find(Node(*iit, 0, 0));
      if (onit == in_nodes.end())
      {
	temp.str("");
	temp<<"Make-Area: Node "<<*iit<<" referred by way "<<it->id
	    <<" is not contained in set \""<<input<<"\".\n";
	runtime_error(temp.str(), cout);
	maps[output] = Set(nodes, ways, relations, areas);
	return;
      }
      ++iit;
    }
    for (; iit != it->members.end(); ++iit)
    {
      set< Node >::const_iterator nnit(in_nodes.find(Node(*iit, 0, 0)));
      if (nnit == in_nodes.end())
      {
	temp.str("");
	temp<<"Make-Area: Node "<<*iit<<" referred by way "<<it->id
	    <<" is not contained in set \""<<input<<"\".\n";
	runtime_error(temp.str(), cout);
	maps[output] = Set(nodes, ways, relations, areas);
	return;
      }
      if (calc_idx(onit->lat) != calc_idx(nnit->lat))
      {
	if (calc_idx(onit->lat) < calc_idx(nnit->lat))
	{
	  area_insert(area, onit->lat, onit->lon, (calc_idx(onit->lat)+1)*10000000,
		      onit->lon + (long long)(nnit->lon - onit->lon)
			  *((calc_idx(onit->lat)+1)*10000000 - onit->lat)/(nnit->lat - onit->lat));
	  lat_intersections[calc_idx(onit->lat)+90].insert
	      (onit->lon + (long long)(nnit->lon - onit->lon)
	      *((calc_idx(onit->lat)+1)*10000000 - onit->lat)/(nnit->lat - onit->lat));
	  for (int i(calc_idx(onit->lat)+1); i < calc_idx(nnit->lat); ++i)
	  {
	    area_insert(area, i*10000000,
			onit->lon + (long long)(nnit->lon - onit->lon)
			    *(i*10000000 - onit->lat)/(nnit->lat - onit->lat),
			      (i+1)*10000000,
			onit->lon + (long long)(nnit->lon - onit->lon)
			    *((i+1)*10000000 - onit->lat)/(nnit->lat - onit->lat));
	    lat_intersections[i+90].insert
		(onit->lon + (long long)(nnit->lon - onit->lon)
		*((i+1)*10000000 - onit->lat)/(nnit->lat - onit->lat));
	  }
	  area_insert(area, calc_idx(nnit->lat)*10000000,
		      onit->lon + (long long)(nnit->lon - onit->lon)
			  *(calc_idx(nnit->lat)*10000000 - onit->lat)/(nnit->lat - onit->lat),
			    nnit->lat, nnit->lon);
	}
	else
	{
	  area_insert(area, nnit->lat, nnit->lon, (calc_idx(nnit->lat)+1)*10000000,
		      nnit->lon + (long long)(onit->lon - nnit->lon)
			  *((calc_idx(nnit->lat)+1)*10000000 - nnit->lat)/(onit->lat - nnit->lat));
	  lat_intersections[calc_idx(nnit->lat)+90].insert
	      (nnit->lon + (long long)(onit->lon - nnit->lon)
	      *((calc_idx(nnit->lat)+1)*10000000 - nnit->lat)/(onit->lat - nnit->lat));
	  for (int i(calc_idx(nnit->lat)+1); i < calc_idx(onit->lat); ++i)
	  {
	    area_insert(area, i*10000000,
			nnit->lon + (long long)(onit->lon - nnit->lon)
			    *(i*10000000 - nnit->lat)/(onit->lat - nnit->lat),
			      (i+1)*10000000,
			       nnit->lon + (long long)(onit->lon - nnit->lon)
				   *((i+1)*10000000 - nnit->lat)/(onit->lat - nnit->lat));
	    lat_intersections[i+90].insert
		(nnit->lon + (long long)(onit->lon - nnit->lon)
		*((i+1)*10000000 - nnit->lat)/(onit->lat - nnit->lat));
	  }
	  area_insert(area, calc_idx(onit->lat)*10000000,
		      nnit->lon + (long long)(onit->lon - nnit->lon)
			  *(calc_idx(onit->lat)*10000000 - nnit->lat)/(onit->lat - nnit->lat),
			    onit->lat, onit->lon);
	}
      }
      else
	area_insert(area, onit->lat, onit->lon, nnit->lat, nnit->lon);
      onit = nnit;
    }
    if (it->members.size() > 0)
    {
      if (node_parity_control.find(onit->id) != node_parity_control.end())
	node_parity_control.erase(onit->id);
      else
	node_parity_control.insert(onit->id);
    }
  }
  if (node_parity_control.size() > 0)
  {
    temp.str("");
    temp<<"Make-Area: Node "<<*(node_parity_control.begin())
	<<" is contained in an odd number of ways.\n";
    runtime_error(temp.str(), cout);
    maps[output] = Set(nodes, ways, relations, areas);
    return;
  }
  for (unsigned int i(0); i < 179; ++i)
  {
    int from(2000000000);
    for (set< int >::const_iterator it(lat_intersections[i].begin());
	 it != lat_intersections[i].end(); ++it)
    {
      if (from == 2000000000)
	from = *it;
      else
      {
	if (floor(((double)from)/100000) == floor(((double)(*it))/100000))
	  area.segments.insert(Line_Segment
	      ((i-89)*10000000, ((double)(from))/10000000,
		(i-89)*10000000, ((double)(*it))/10000000));
	else
	{
	  area.segments.insert(Line_Segment
	      ((i-89)*10000000, from,
		(i-89)*10000000, ((int)(floor(((double)from)/100000))+1)*100000));
	  for (int j((int)(floor(((double)from)/100000))+1);
		      j < floor(((double)(*it))/100000); ++j)
	    area.segments.insert(Line_Segment
		((i-89)*10000000, j*100000, (i-89)*10000000, (j+1)*100000));
	  area.segments.insert(Line_Segment
	      ((i-89)*10000000, ((int)floor(((double)(*it))/100000))*100000,
		(i-89)*10000000, *it));
	}
	from = 2000000000;
      }
    }
  }
  
  temp.str("");
  temp<<"insert areas values ("<<area.id<<", "<<pivot_id<<", "<<pivot_type<<')';
  mysql_query(mysql, temp.str().c_str());
  
  ofstream area_ways_out("/tmp/db_area_area_ways.tsv");
  for (set< Way >::const_iterator it(in_ways.begin());
       it != in_ways.end(); ++it)
    area_ways_out<<area.id<<'\t'<<it->id<<'\n';
  area_ways_out.close();
  mysql_query(mysql, "load data local infile '/tmp/db_area_area_ways.tsv' into table area_ways");
  
  ofstream area_segments_out("/tmp/db_area_area_segments.tsv");
  for (set< Line_Segment >::const_iterator it(area.segments.begin());
       it != area.segments.end(); ++it)
    area_segments_out<<area.id<<'\t'<<calc_idx(it->west_lat)<<'\t'
	<<it->west_lat<<'\t'<<it->west_lon<<'\t'
	<<it->east_lat<<'\t'<<it->east_lon<<'\n';
  area_segments_out.close();
  mysql_query(mysql, "load data local infile '/tmp/db_area_area_segments.tsv' into table area_segments");
  
  temp.str("");
  if (pivot_type == NODE)
    temp<<"insert into area_tags "
	<<"select "<<area.id<<", node_tags.key_, node_tags.value_ "
	<<"from node_tags where node_tags.id = "<<pivot_id;
  else if (pivot_type == WAY)
    temp<<"insert into area_tags "
	<<"select "<<area.id<<", way_tags.key_, way_tags.value_ "
	<<"from way_tags where way_tags.id = "<<pivot_id;
  else if (pivot_type == RELATION)
    temp<<"insert into area_tags "
	<<"select "<<area.id<<", relation_tags.key_, relation_tags.value_ "
	<<"from relation_tags where relation_tags.id = "<<pivot_id;
  if (temp.str() != "")
    mysql_query(mysql, temp.str().c_str());
  
  ostringstream stack;
  for (vector< pair< int, int > >::const_iterator it(get_stack().begin());
       it != get_stack().end(); ++it)
    stack<<it->first<<' '<<it->second<<' ';
  temp.str("");
  temp<<"insert area_origins values ("<<area.id<<", "
      <<get_rule_id()<<", "
      <<this->get_line_number()<<", '"
      <<stack.str()<<"')";
  mysql_query(mysql, temp.str().c_str());
  
  area.segments.clear(); //TODO
  areas.insert(area);
  maps[output] = Set(nodes, ways, relations, areas);
}
