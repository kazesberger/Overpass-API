#include "data_from_osc.h"


template< typename Obj >
class Compare_Vector
{
public:
  Compare_Vector(const std::string& title_) : title(title_)
  {
    std::cerr<<title<<" ... ";
  }

  Compare_Vector& operator()(const Obj& obj)
  {
    target.push_back(obj);
    return *this;
  }

  bool operator()(const std::vector< Obj >& candidate) const
  {
    bool all_ok = true;
    if (candidate.size() != target.size())
    {
      notify_failed(all_ok);
      std::cerr<<title<<": "<<target.size()<<" elements expected, "<<candidate.size()<<" elements found.\n";
    }
    for (decltype(target.size()) i = 0; i < target.size() && i < candidate.size(); ++i)
    {
      if (!(target[i] == candidate[i]))
      {
        notify_failed(all_ok);
        std::cerr<<title<<", element "<<i<<": found element different from expected one.\n";
      }
    }
    if (all_ok)
      std::cerr<<"ok.\n";
    return all_ok;
  }

private:
  static void notify_failed(bool& all_ok)
  {
    if (all_ok)
    {
      std::cerr<<"FAILED!\n";
      all_ok = false;
    }
  }

  std::string title;
  std::vector< Obj > target;
};


template< typename Key, typename Value >
class Compare_Map
{
public:
  Compare_Map(const std::string& title_) : title(title_)
  {
    std::cerr<<title<<" ... ";
  }

  Compare_Map& operator()(const Key& key, const Value& value)
  {
    target[key] = value;
    return *this;
  }

  bool operator()(const std::map< Key, Value >& candidate) const
  {
    bool all_ok = true;
    if (candidate.size() != target.size())
    {
      notify_failed(all_ok);
      std::cerr<<title<<": "<<target.size()<<" elements expected, "<<candidate.size()<<" elements found.\n";
    }
    auto i_target = target.begin();
    auto i_candidate = candidate.begin();
    while (i_target != target.end())
    {
      while (i_candidate != candidate.end() && i_candidate->first < i_target->first)
      {
        notify_failed(all_ok);
        std::cerr<<title<<": unexpected key skipped.\n";
        ++i_candidate;
      }
      if (i_candidate == candidate.end() || i_target->first < i_candidate->first)
      {
        notify_failed(all_ok);
        std::cerr<<title<<": expected key missing.\n";
      }
      if (i_candidate != candidate.end())
      {
        if (!(i_candidate->second == i_target->second))
        {
          notify_failed(all_ok);
          std::cerr<<title<<": values differ.\n";
        }
        ++i_candidate;
      }
      ++i_target;
    }
    if (all_ok)
      std::cerr<<"ok.\n";
    return all_ok;
  }

private:
  static void notify_failed(bool& all_ok)
  {
    if (all_ok)
    {
      std::cerr<<"FAILED!\n";
      all_ok = false;
    }
  }

  std::string title;
  std::map< Key, Value > target;
};


int main(int argc, char* args[])
{
  {
    std::cerr<<"Test empty input:\n";
    Data_From_Osc data_from_osc;
    bool all_ok = true;
    all_ok &= Compare_Vector< std::pair< Node_Skeleton::Id_Type, uint64_t > >("node_id_dates")
        (data_from_osc.node_id_dates());
    all_ok &= Compare_Vector< Node_Pre_Event >("node_pre_events")
        (data_from_osc.node_pre_events().data);
    all_ok &= Compare_Map< Uint31_Index, Coord_Dates_Per_Idx >("node_coord_dates")
        (data_from_osc.node_coord_dates());
  }
  {
    std::cerr<<"\nTest one node:\n";
    Data_From_Osc data_from_osc;

    OSM_Element_Metadata meta;
    meta.timestamp = 1000;
    meta.version = 6;
    data_from_osc.set_node(Node(Uint64(496ull), 51.25, 7.15), &meta);

    Data_By_Id< Node_Skeleton >::Entry entry(
        ll_upper_(51.25, 7.15), Node_Skeleton(496ull), OSM_Element_Metadata_Skeleton< Uint64 >(496ull, meta));
    bool all_ok = true;
    all_ok &= Compare_Vector< std::pair< Node_Skeleton::Id_Type, uint64_t > >("node_id_dates")
        (std::make_pair(Uint64(496), 1000))
        (data_from_osc.node_id_dates());
    all_ok &= Compare_Vector< Node_Pre_Event >("node_pre_events")
        (Node_Pre_Event(entry))
        (data_from_osc.node_pre_events().data);
    all_ok &= Compare_Map< Uint31_Index, Coord_Dates_Per_Idx >("node_coord_dates")
        (ll_upper_(51.25, 7.15), std::vector< Attic< Uint32 > >(1, Attic< Uint32 >(ll_lower(51.25, 7.15), 1000)))
        (data_from_osc.node_coord_dates());
  }
  {
    std::cerr<<"\nTest one way:\n";
    Data_From_Osc data_from_osc;

    OSM_Element_Metadata meta;
    meta.timestamp = 1001;
    meta.version = 5;
    Way way(496u);
    way.nds.push_back(Uint64(496002ull));
    way.nds.push_back(Uint64(496001ull));
    data_from_osc.set_way(way, &meta);

    bool all_ok = true;
    all_ok &= Compare_Vector< std::pair< Node_Skeleton::Id_Type, uint64_t > >("node_id_dates")
        (std::make_pair(Uint64(496001), 1001))
        (std::make_pair(Uint64(496002), 1001))
        (data_from_osc.node_id_dates());
    all_ok &= Compare_Vector< Node_Pre_Event >("node_pre_events")
        (data_from_osc.node_pre_events().data);
    all_ok &= Compare_Map< Uint31_Index, Coord_Dates_Per_Idx >("node_coord_dates")
        (data_from_osc.node_coord_dates());
  }
  {
    std::cerr<<"\nTest node and way interplay:\n";
    Data_From_Osc data_from_osc;

    OSM_Element_Metadata meta;
    meta.timestamp = 1009;
    meta.version = 9;
    data_from_osc.set_node(Node(Uint64(496001ull), 51.25001, 7.15), &meta);
    meta.timestamp = 1001;
    meta.version = 1;
    data_from_osc.set_node(Node(Uint64(496002ull), 51.25002, 7.15), &meta);
    meta.timestamp = 1005;
    meta.version = 5;
    Way way(496u);
    way.nds.push_back(Uint64(496002ull));
    way.nds.push_back(Uint64(496001ull));
    data_from_osc.set_way(way, &meta);

    meta.timestamp = 1009;
    meta.version = 9;
    Data_By_Id< Node_Skeleton >::Entry entry1(
        ll_upper_(51.25, 7.15), Node_Skeleton(496001ull),
        OSM_Element_Metadata_Skeleton< Uint64 >(496001ull, meta));
    meta.timestamp = 1001;
    meta.version = 1;
    Data_By_Id< Node_Skeleton >::Entry entry2(
        ll_upper_(51.25, 7.15), Node_Skeleton(496002ull),
        OSM_Element_Metadata_Skeleton< Uint64 >(496002ull, meta));
    std::vector< Attic< Uint32 > > coord_dates;
    coord_dates.push_back(Attic< Uint32>(ll_lower(51.25001, 7.15), 1009));
    coord_dates.push_back(Attic< Uint32>(ll_lower(51.25002, 7.15), 1001));
    bool all_ok = true;
    all_ok &= Compare_Vector< std::pair< Node_Skeleton::Id_Type, uint64_t > >("node_id_dates")
        (std::make_pair(Uint64(496001), 1005))
        (std::make_pair(Uint64(496002), 1001))
        (data_from_osc.node_id_dates());
    all_ok &= Compare_Vector< Node_Pre_Event >("node_pre_events")
        (Node_Pre_Event(entry1))
        (Node_Pre_Event(entry2))
        (data_from_osc.node_pre_events().data);
    all_ok &= Compare_Map< Uint31_Index, Coord_Dates_Per_Idx >("node_coord_dates")
        (ll_upper_(51.25, 7.15), coord_dates)
        (data_from_osc.node_coord_dates());
  }
  {
    std::cerr<<"\nTest multiple nodes:\n";
    Data_From_Osc data_from_osc;

    OSM_Element_Metadata meta;
    meta.timestamp = 1004;
    meta.version = 4;
    data_from_osc.set_node(Node(Uint64(494ull), 51.25, 7.45), &meta);
    meta.timestamp = 1005;
    meta.version = 5;
    data_from_osc.set_node(Node(Uint64(495ull), 51.25, 7.55), &meta);
    meta.timestamp = 1006;
    meta.version = 6;
    data_from_osc.set_node_deleted(Uint64(496ull), &meta);

    meta.timestamp = 1004;
    meta.version = 4;
    Data_By_Id< Node_Skeleton >::Entry entry4(
        ll_upper_(51.25, 7.45), Node_Skeleton(494ull), OSM_Element_Metadata_Skeleton< Uint64 >(494ull, meta));
    meta.timestamp = 1005;
    meta.version = 5;
    Data_By_Id< Node_Skeleton >::Entry entry5(
        ll_upper_(51.25, 7.55), Node_Skeleton(495ull), OSM_Element_Metadata_Skeleton< Uint64 >(495ull, meta));
    meta.timestamp = 1006;
    meta.version = 6;
    Data_By_Id< Node_Skeleton >::Entry entry6(
        Uint31_Index(0u), Node_Skeleton(496ull), OSM_Element_Metadata_Skeleton< Uint64 >(496ull, meta));
    bool all_ok = true;
    all_ok &= Compare_Vector< std::pair< Node_Skeleton::Id_Type, uint64_t > >("node_id_dates")
        (std::make_pair(Uint64(494), 1004))
        (std::make_pair(Uint64(495), 1005))
        (std::make_pair(Uint64(496), 1006))
        (data_from_osc.node_id_dates());
    all_ok &= Compare_Vector< Node_Pre_Event >("node_pre_events")
        (Node_Pre_Event(entry4))
        (Node_Pre_Event(entry5))
        (Node_Pre_Event(entry6))
        (data_from_osc.node_pre_events().data);
    all_ok &= Compare_Map< Uint31_Index, Coord_Dates_Per_Idx >("node_coord_dates")
        (ll_upper_(51.25, 7.45), std::vector< Attic< Uint32 > >(1, Attic< Uint32 >(ll_lower(51.25, 7.45), 1004)))
        (ll_upper_(51.25, 7.55), std::vector< Attic< Uint32 > >(1, Attic< Uint32 >(ll_lower(51.25, 7.55), 1005)))
        (data_from_osc.node_coord_dates());
  }
  {
    std::cerr<<"\nTest multiple versions of a node:\n";
    Data_From_Osc data_from_osc;

    OSM_Element_Metadata meta;
    meta.timestamp = 1100;
    meta.version = 1;
    data_from_osc.set_node(Node(Uint64(496ull), 51.25, 7.15), &meta);
    Data_By_Id< Node_Skeleton >::Entry entry1(
        ll_upper_(51.25, 7.15), Node_Skeleton(496ull), OSM_Element_Metadata_Skeleton< Uint64 >(496ull, meta));
    meta.timestamp = 1200;
    meta.version = 2;
    data_from_osc.set_node(Node(Uint64(496ull), 51.25, 7.25), &meta);
    Data_By_Id< Node_Skeleton >::Entry entry2(
        ll_upper_(51.25, 7.25), Node_Skeleton(496ull), OSM_Element_Metadata_Skeleton< Uint64 >(496ull, meta));
    meta.timestamp = 1300;
    meta.version = 3;
    data_from_osc.set_node_deleted(Uint64(496ull), &meta);
    Data_By_Id< Node_Skeleton >::Entry entry3(
        Uint31_Index(0u), Node_Skeleton(496ull), OSM_Element_Metadata_Skeleton< Uint64 >(496ull, meta));
    meta.timestamp = 1401;
    meta.version = 4;
    data_from_osc.set_node(Node(Uint64(496ull), 51.25, 7.15), &meta);
    Data_By_Id< Node_Skeleton >::Entry entry4(
        ll_upper_(51.25, 7.15), Node_Skeleton(496ull), OSM_Element_Metadata_Skeleton< Uint64 >(496ull, meta));
    meta.timestamp = 1502;
    meta.version = 5;
    data_from_osc.set_node(Node(Uint64(496ull), 51.25, 7.25001), &meta);
    Data_By_Id< Node_Skeleton >::Entry entry5(
        ll_upper_(51.25, 7.25001), Node_Skeleton(496ull), OSM_Element_Metadata_Skeleton< Uint64 >(496ull, meta));

    std::vector< Attic< Uint32 > > coord_dates;
    coord_dates.push_back(Attic< Uint32>(ll_lower(51.25, 7.25), 1200));
    coord_dates.push_back(Attic< Uint32>(ll_lower(51.25, 7.25001), 1502));
    bool all_ok = true;
    all_ok &= Compare_Vector< std::pair< Node_Skeleton::Id_Type, uint64_t > >("node_id_dates")
        (std::make_pair(Uint64(496), 1100))
        (data_from_osc.node_id_dates());
    all_ok &= Compare_Vector< Node_Pre_Event >("node_pre_events")
        (Node_Pre_Event(entry1, 1200))
        (Node_Pre_Event(entry2, 1300))
        (Node_Pre_Event(entry3, 1401))
        (Node_Pre_Event(entry4, 1502))
        (Node_Pre_Event(entry5))
        (data_from_osc.node_pre_events().data);
    all_ok &= Compare_Map< Uint31_Index, Coord_Dates_Per_Idx >("node_coord_dates")
        (ll_upper_(51.25, 7.15), std::vector< Attic< Uint32 > >(1, Attic< Uint32 >(ll_lower(51.25, 7.15), 1100)))
        (ll_upper_(51.25, 7.25), coord_dates)
        (data_from_osc.node_coord_dates());
  }

  return 0;
}
