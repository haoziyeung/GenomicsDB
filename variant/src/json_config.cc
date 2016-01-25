//Enable asserts
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "json_config.h"

#define VERIFY_OR_THROW(X) if(!(X)) throw RunConfigException(#X);

void JSONConfigBase::clear()
{
  m_workspaces.clear();
  m_array_names.clear();
  m_column_ranges.clear();
  m_row_ranges.clear();
  m_attributes.clear();
}

void JSONConfigBase::read_from_file(const std::string& filename)
{
  std::ifstream ifs(filename.c_str());
  VERIFY_OR_THROW(ifs.is_open());
  std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  m_json.Parse(str.c_str());
  //Workspace
  if(m_json.HasMember("workspace"))
  {
    const rapidjson::Value& workspace = m_json["workspace"];
    //workspace could be an array, one workspace dir for every rank
    if(workspace.IsArray())
    {
      for(rapidjson::SizeType i=0;i<workspace.Size();++i)
      {
        VERIFY_OR_THROW(workspace[i].IsString());
        m_workspaces.push_back(workspace[i].GetString());
      }
    }
    else //workspace is simply a string
    {
      VERIFY_OR_THROW(workspace.IsString());
      m_workspaces.push_back(workspace.GetString());
      m_single_workspace_path = true;
    }
  }
  //Array
  if(m_json.HasMember("array"))
  {
    const rapidjson::Value& array_name = m_json["array"];
    //array could be an array, one array dir for every rank
    if(array_name.IsArray())
    {
      for(rapidjson::SizeType i=0;i<array_name.Size();++i)
      {
        VERIFY_OR_THROW(array_name[i].IsString());
        m_array_names.push_back(array_name[i].GetString());
      }
    }
    else //array is simply a string
    {
      VERIFY_OR_THROW(array_name.IsString());
      m_array_names.push_back(array_name.GetString());
      m_single_array_name = true;
    }
  }
  VERIFY_OR_THROW(m_json.HasMember("query_column_ranges") || m_json.HasMember("column_partitions")
      || m_json.HasMember("scan_full"));
  if(m_json.HasMember("scan_full"))
    m_scan_whole_array = true;
  else
  {
    VERIFY_OR_THROW((!m_json.HasMember("query_column_ranges") || !m_json.HasMember("column_partitions")) &&
        "Cannot use both \"query_column_ranges\" and \"column_partitions\" simultaneously");
    //Query columns
    //Example:  [ [ [0,5], 45 ], [ 76, 87 ] ]
    //This means that rank 0 will have 2 query intervals: [0-5] and [45-45] and rank 1 will have
    //2 intervals [76-76] and [87-87]
    //But you could have a single innermost list - with this option all ranks will query the same list 
    if(m_json.HasMember("query_column_ranges"))
    {
      const rapidjson::Value& q1 = m_json["query_column_ranges"];
      VERIFY_OR_THROW(q1.IsArray());
      if(q1.Size() == 1)
        m_single_query_column_ranges_vector = true;
      m_column_ranges.resize(q1.Size());
      for(rapidjson::SizeType i=0;i<q1.Size();++i)
      {
        const rapidjson::Value& q2 = q1[i];
        VERIFY_OR_THROW(q2.IsArray());
        m_column_ranges[i].resize(q2.Size());
        for(rapidjson::SizeType j=0;j<q2.Size();++j)
        {
          const rapidjson::Value& q3 = q2[j];
          //q3 is list of 2 elements to represent query interval
          if(q3.IsArray())
          {
            VERIFY_OR_THROW(q3.Size() == 2);
            VERIFY_OR_THROW(q3[0u].IsInt64());
            VERIFY_OR_THROW(q3[1u].IsInt64());
            m_column_ranges[i][j].first = q3[0u].GetInt64();
            m_column_ranges[i][j].second = q3[1u].GetInt64();
          }
          else //single position
          {
            VERIFY_OR_THROW(q3.IsInt64());
            m_column_ranges[i][j].first = q3.GetInt64();
            m_column_ranges[i][j].second = q3.GetInt64();
          }
          if(m_column_ranges[i][j].first > m_column_ranges[i][j].second)
            std::swap<int64_t>(m_column_ranges[i][j].first, m_column_ranges[i][j].second);
        }
      }
    }
    else        //Must have column_partitions
    {
      m_column_partitions_specified = true;
      //column_partitions_dict itself a dictionary of the form { "0" : { "begin" : <value> } }
      auto& column_partitions_dict = m_json["column_partitions"];
      VERIFY_OR_THROW(column_partitions_dict.IsObject());
      m_sorted_column_partitions.resize(column_partitions_dict.MemberCount());
      m_column_ranges.resize(column_partitions_dict.MemberCount());
      auto partition_idx = 0u;
      std::unordered_map<int64_t, unsigned> begin_to_idx;
      auto workspace_string = m_single_workspace_path ? m_workspaces[0] : "";
      auto array_name_string = m_single_array_name ? m_array_names[0] : "";
      for(auto b=column_partitions_dict.MemberBegin(), e=column_partitions_dict.MemberEnd();b!=e;++b,++partition_idx)
      {
        const auto& curr_obj = *b;
        //{ "begin" : <Val> }
        const auto& curr_partition_info_dict = curr_obj.value;
        VERIFY_OR_THROW(curr_partition_info_dict.IsObject());
        VERIFY_OR_THROW(curr_partition_info_dict.HasMember("begin"));
        m_column_ranges[partition_idx].resize(1);      //only 1 std::pair
        m_column_ranges[partition_idx][0].first = curr_partition_info_dict["begin"].GetInt64();
        m_column_ranges[partition_idx][0].second = INT64_MAX;
        if(curr_partition_info_dict.HasMember("end"))
          m_column_ranges[partition_idx][0].second = curr_partition_info_dict["end"].GetInt64();
        if(m_column_ranges[partition_idx][0].first > m_column_ranges[partition_idx][0].second)
          std::swap<int64_t>(m_column_ranges[partition_idx][0].first, m_column_ranges[partition_idx][0].second);
        if(curr_partition_info_dict.HasMember("workspace"))
        {
          if(column_partitions_dict.MemberCount() > m_workspaces.size())
            m_workspaces.resize(column_partitions_dict.MemberCount(), workspace_string);
          m_workspaces[partition_idx] = curr_partition_info_dict["workspace"].GetString();
          m_single_workspace_path = false;
        }
        if(curr_partition_info_dict.HasMember("array"))
        {
          if(column_partitions_dict.MemberCount() >= m_array_names.size())
            m_array_names.resize(column_partitions_dict.MemberCount(), array_name_string);
          m_array_names[partition_idx] = curr_partition_info_dict["array"].GetString();
          m_single_array_name = false;
        }
        //Mapping from begin pos to index
        begin_to_idx[m_column_ranges[partition_idx][0].first] = partition_idx;
        m_sorted_column_partitions[partition_idx].first = m_column_ranges[partition_idx][0].first;
        m_sorted_column_partitions[partition_idx].second = m_column_ranges[partition_idx][0].second;
      }
      //Sort in ascending order
      std::sort(m_sorted_column_partitions.begin(), m_sorted_column_partitions.end(), ColumnRangeCompare);
      //Set end value if not valid
      for(auto i=0ull;i+1u<m_sorted_column_partitions.size();++i)
      {
        VERIFY_OR_THROW(m_sorted_column_partitions[i].first != m_sorted_column_partitions[i+1u].first
            && "Cannot have two column partitions with the same begin value");
        if(m_sorted_column_partitions[i].second >= m_sorted_column_partitions[i+1u].first)
          m_sorted_column_partitions[i].second = m_sorted_column_partitions[i+1u].first-1;
        auto idx = begin_to_idx[m_sorted_column_partitions[i].first];
        m_column_ranges[idx][0].second = m_sorted_column_partitions[i].second;
      }
    }
  }
  //Query rows
  //Example:  [ [ [0,5], 45 ], [ 76, 87 ] ]
  //This means that rank 0 will query rows: [0-5] and [45-45] and rank 1 will have
  //2 intervals [76-76] and [87-87]
  //But you could have a single innermost list - with this option all ranks will query the same list 
  if(m_json.HasMember("query_row_ranges"))
  {
    const rapidjson::Value& q1 = m_json["query_row_ranges"];
    VERIFY_OR_THROW(q1.IsArray());
    if(q1.Size() == 1)
      m_single_query_row_ranges_vector = true;
    m_row_ranges.resize(q1.Size());
    for(rapidjson::SizeType i=0;i<q1.Size();++i)
    {
      const rapidjson::Value& q2 = q1[i];
      VERIFY_OR_THROW(q2.IsArray());
      m_row_ranges[i].resize(q2.Size());
      for(rapidjson::SizeType j=0;j<q2.Size();++j)
      {
        const rapidjson::Value& q3 = q2[j];
        //q3 is list of 2 elements to represent query row interval
        if(q3.IsArray())
        {
          VERIFY_OR_THROW(q3.Size() == 2);
          VERIFY_OR_THROW(q3[0u].IsInt64());
          VERIFY_OR_THROW(q3[1u].IsInt64());
          m_row_ranges[i][j].first = q3[0u].GetInt64();
          m_row_ranges[i][j].second = q3[1u].GetInt64();
        }
        else //single position
        {
          VERIFY_OR_THROW(q3.IsInt64());
          m_row_ranges[i][j].first = q3.GetInt64();
          m_row_ranges[i][j].second = q3.GetInt64();
        }
        if(m_row_ranges[i][j].first > m_row_ranges[i][j].second)
          std::swap<int64_t>(m_row_ranges[i][j].first, m_row_ranges[i][j].second);
      }
    }
  }
  if(m_json.HasMember("query_attributes"))
  {
    const rapidjson::Value& q1 = m_json["query_attributes"];
    VERIFY_OR_THROW(q1.IsArray());
    m_attributes.resize(q1.Size());
    for(rapidjson::SizeType i=0;i<q1.Size();++i)
    {
      const rapidjson::Value& q2 = q1[i];
      VERIFY_OR_THROW(q2.IsString());
      m_attributes[i] = std::move(std::string(q2.GetString()));
    }
  }
}

const std::string& JSONConfigBase::get_workspace(const int rank) const
{
  VERIFY_OR_THROW((m_single_workspace_path || static_cast<size_t>(rank) < m_workspaces.size())
     && ("Workspace not defined for rank "+std::to_string(rank)).c_str());
  if(m_single_workspace_path)
    return m_workspaces[0];
  return m_workspaces[rank];
}

const std::string& JSONConfigBase::get_array_name(const int rank) const
{
  VERIFY_OR_THROW((m_single_array_name || static_cast<size_t>(rank) < m_array_names.size())
      && ("Could not find array for rank "+std::to_string(rank)).c_str());
  if(m_single_array_name)
    return m_array_names[0];
  return m_array_names[rank];
}

ColumnRange JSONConfigBase::get_column_partition(const int rank, const unsigned idx) const
{
  auto fixed_rank = m_single_query_column_ranges_vector ? 0 : rank;
  VERIFY_OR_THROW(static_cast<size_t>(fixed_rank) < m_column_ranges.size());
  VERIFY_OR_THROW(idx < m_column_ranges[fixed_rank].size());
  return m_column_ranges[fixed_rank][idx];
}

void JSONBasicQueryConfig::read_from_file(const std::string& filename, VariantQueryConfig& query_config, const int rank)
{

  JSONConfigBase::read_from_file(filename);
  //Workspace
  VERIFY_OR_THROW(m_workspaces.size() && "No workspace specified");
  VERIFY_OR_THROW((m_single_workspace_path || static_cast<size_t>(rank) < m_workspaces.size())
      && ("Could not find workspace for rank "+std::to_string(rank)).c_str());
  auto& workspace = m_single_workspace_path ? m_workspaces[0] : m_workspaces[rank];
  VERIFY_OR_THROW(workspace != "" && "Empty workspace string");
  //Array
  VERIFY_OR_THROW(m_array_names.size() && "No array specified");
  VERIFY_OR_THROW((m_single_array_name || static_cast<size_t>(rank) < m_array_names.size())
      && ("Could not find array for rank "+std::to_string(rank)).c_str());
  auto& array_name = m_single_array_name ? m_array_names[0] : m_array_names[rank];
  VERIFY_OR_THROW(array_name != "" && "Empty array name");
  //Query columns
  VERIFY_OR_THROW((m_column_ranges.size() || m_scan_whole_array) && "Query column ranges not specified");
  if(!m_scan_whole_array)
  {
    VERIFY_OR_THROW((m_single_query_column_ranges_vector || static_cast<size_t>(rank) < m_column_ranges.size())
        && "Rank >= query column ranges vector size");
    auto& column_ranges_vector = m_single_query_column_ranges_vector ? m_column_ranges[0] : m_column_ranges[rank];
    for(auto& range : column_ranges_vector)
      query_config.add_column_interval_to_query(range.first, range.second);
  }
  //Query rows
  if(m_row_ranges.size())
  {
    VERIFY_OR_THROW((m_single_query_row_ranges_vector || static_cast<size_t>(rank) < m_row_ranges.size())
        && "Rank >= query row ranges vector size");
    const auto& row_ranges_vector = m_single_query_row_ranges_vector ? m_row_ranges[0] : m_row_ranges[rank];
    std::vector<int64_t> row_idxs;
    for(const auto& range : row_ranges_vector)
    {
      auto j = row_idxs.size();
      row_idxs.resize(row_idxs.size() + (range.second - range.first + 1ll));
      for(auto i=range.first;i<=range.second;++i,++j)
        row_idxs[j] = i;
    }
    query_config.set_rows_to_query(row_idxs);
  }
  //Attributes
  VERIFY_OR_THROW(m_attributes.size() && "Attributes to query not specified");
  query_config.set_attributes_to_query(m_attributes);
}
   
#ifdef HTSDIR

void JSONVCFAdapterConfig::read_from_file(const std::string& filename,
    VCFAdapter& vcf_adapter, std::string output_format, const int rank)
{
  JSONConfigBase::read_from_file(filename);
  //VCF header filename
  VERIFY_OR_THROW(m_json.HasMember("vcf_header_filename"));
  {
    const rapidjson::Value& v = m_json["vcf_header_filename"];
    //vcf_header_filename could be an array, one vcf_header_filename location for every rank
    if(v.IsArray())
    {
      VERIFY_OR_THROW(rank < v.Size());
      VERIFY_OR_THROW(v[rank].IsString());
      m_vcf_header_filename = v[rank].GetString();
    }
    else //vcf_header_filename is simply a string
    {
      VERIFY_OR_THROW(v.IsString());
      m_vcf_header_filename = v.GetString();
    }
  }
  //VCF output filename
  if(m_json.HasMember("vcf_output_filename"))
  {
    const rapidjson::Value& v = m_json["vcf_output_filename"];
    //vcf_output_filename could be an array, one vcf_output_filename location for every rank
    if(v.IsArray())
    {
      VERIFY_OR_THROW(rank < v.Size());
      VERIFY_OR_THROW(v[rank].IsString());
      m_vcf_output_filename = v[rank].GetString();
    }
    else //vcf_output_filename is simply a string
    {
      VERIFY_OR_THROW(v.IsString());
      m_vcf_output_filename = v.GetString();
    }
  }
  else
    m_vcf_output_filename = "-";        //stdout
  //VCF output could also be specified in column partitions
  if(m_json.HasMember("column_partitions"))
  {
    //column_partitions_dict itself a dictionary of the form { "0" : { "begin" : <value> } }
    auto& column_partitions_dict = m_json["column_partitions"];
    VERIFY_OR_THROW(column_partitions_dict.IsObject());
    if(rank < column_partitions_dict.MemberCount())
    {
      auto partition_idx = 0;
      for(auto b=column_partitions_dict.MemberBegin(), e=column_partitions_dict.MemberEnd();b!=e;++b,++partition_idx)
        if(partition_idx == rank)
        {
          //*b if of the form (key, value) -- "0" : {"begin": x}
          auto& curr_partition_info_dict = (*b).value;
          VERIFY_OR_THROW(curr_partition_info_dict.IsObject());
          VERIFY_OR_THROW(curr_partition_info_dict.HasMember("begin"));
          if(curr_partition_info_dict.HasMember("vcf_output_filename"))
          {
            VERIFY_OR_THROW(curr_partition_info_dict["vcf_output_filename"].IsString());
            m_vcf_output_filename = curr_partition_info_dict["vcf_output_filename"].GetString();
          }
        }
    }
  }
  //Reference genome
  VERIFY_OR_THROW(m_json.HasMember("reference_genome"));
  {
    const rapidjson::Value& v = m_json["reference_genome"];
    //reference_genome could be an array, one reference_genome location for every rank
    if(v.IsArray())
    {
      VERIFY_OR_THROW(rank < v.Size());
      VERIFY_OR_THROW(v[rank].IsString());
      m_reference_genome = v[rank].GetString();
    }
    else //reference_genome is simply a string
    {
      VERIFY_OR_THROW(v.IsString());
      m_reference_genome = v.GetString();
    }
  }
  //Output format - if arg not empty
  if(output_format == "" && m_json.HasMember("vcf_output_format"))
    output_format = m_json["vcf_output_format"].GetString(); 
  vcf_adapter.initialize(m_reference_genome, m_vcf_header_filename, m_vcf_output_filename, output_format);
}

void JSONVCFAdapterQueryConfig::read_from_file(const std::string& filename, VariantQueryConfig& query_config,
        VCFAdapter& vcf_adapter, FileBasedVidMapper& id_mapper,
        std::string output_format, const int rank)
{
  JSONBasicQueryConfig::read_from_file(filename, query_config, rank);
  JSONVCFAdapterConfig::read_from_file(filename, vcf_adapter, output_format, rank);
  //Over-ride callset mapping file in top-level config if necessary
  std::string callset_mapping_file="";
  if(JSONBasicQueryConfig::m_json.HasMember("callset_mapping_file"))
  {
    const rapidjson::Value& v = JSONBasicQueryConfig::m_json["callset_mapping_file"];
    //Could be array - one for each process
    if(v.IsArray())
    {
      VERIFY_OR_THROW(rank < v.Size());
      VERIFY_OR_THROW(v[rank].IsString());
      callset_mapping_file = v[rank].GetString();
    }
    else
    {
      VERIFY_OR_THROW(v.IsString());
      callset_mapping_file = v.GetString();
    }
  }
  //contig and callset id mapping
  VERIFY_OR_THROW(JSONBasicQueryConfig::m_json.HasMember("vid_mapping_file"));
  {
    const rapidjson::Value& v = JSONBasicQueryConfig::m_json["vid_mapping_file"];
    //Could be array - one for each process
    if(v.IsArray())
    {
      VERIFY_OR_THROW(rank < v.Size());
      VERIFY_OR_THROW(v[rank].IsString());
      id_mapper = std::move(FileBasedVidMapper(v[rank].GetString(), callset_mapping_file));
    }
    else //or single string for all processes
    {
      VERIFY_OR_THROW(v.IsString());
      id_mapper = std::move(FileBasedVidMapper(v.GetString(), callset_mapping_file));
    }
  }
}
#endif
