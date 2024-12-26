#include "workload/fused-workload-dependency-analyzer.hpp"


namespace problem
{

FusedWorkloadDependencyAnalyzer::FusedWorkloadDependencyAnalyzer(
  const FusedWorkload& workload
) : workload_(workload)
{
}

std::vector<std::vector<EinsumId>>
FusedWorkloadDependencyAnalyzer::FindEinsumDependencyChain(
  EinsumId src,
  EinsumId dst
) const
{
  // The strategy used here is to use depth-first search to find all paths.
  // The stack keeps track of (node, all paths to node) pairs.
  std::vector<std::pair<EinsumId, std::vector<EinsumId>>>
  dfs_stack = {{src, {src}}};
  std::vector<std::vector<EinsumId>> result;

  if (src == dst)
  {
    result.emplace_back(std::vector({src}));
  }

  while (dfs_stack.size() > 0)
  {
    const auto [cur_einsum, cur_einsum_path] = std::move(dfs_stack.back());
    dfs_stack.pop_back();

    const auto cur_einsum_outputs = workload_.TensorsWrittenByEinsum(cur_einsum);
    for (const auto& cur_einsum_output : cur_einsum_outputs)
    {
      const auto readers = workload_.ReaderEinsums(cur_einsum_output);
      for (const auto neighbor_einsum : readers)
      {
        auto path_to_neighbor = cur_einsum_path;
        path_to_neighbor.emplace_back(neighbor_einsum);

        if (neighbor_einsum == dst)
        {
          result.emplace_back(path_to_neighbor);
        }
        else
        {
          const auto it = std::find(cur_einsum_path.begin(),
                                    cur_einsum_path.end(),
                                    neighbor_einsum);
          if (it == cur_einsum_path.end())  // Only keep searching if not acyclic
          {
            dfs_stack.emplace_back(
              std::make_pair(neighbor_einsum, path_to_neighbor)
            );
          }
        }

      }
    }
  }

  return result;
}

bool FusedWorkloadDependencyAnalyzer::EinsumDimIsDirectlyRelevantToTensor(
  EinsumId einsum,
  DimensionId einsum_dim,
  DataSpaceId dspace
) const
{
  const auto& read_tensors = workload_.TensorsReadByEinsum(einsum);
  const auto is_read_tensor = read_tensors.find(dspace) != read_tensors.end();

  const auto& written_tensors = workload_.TensorsWrittenByEinsum(einsum);
  const auto is_written_tensor =
      written_tensors.find(dspace) != written_tensors.end();

  if (!is_read_tensor && !is_written_tensor)
  {
    return false;
  }

  const auto dim_idx = workload_.EinsumDimToIdx(einsum).at(einsum_dim);
  const auto& projection = workload_.Accesses(einsum, dspace);

  const auto dim_involved = isl_map_involves_dims(projection.get(),
                                                  isl_dim_in,
                                                  dim_idx,
                                                  1);
  return dim_involved == isl_bool_true;
}

bool FusedWorkloadDependencyAnalyzer::EinsumDimIsRelevantToTensor(
  EinsumId einsum,
  DimensionId einsum_dim,
  DataSpaceId dspace
) const
{
  const auto dim_idx = workload_.EinsumDimToIdx(einsum).at(einsum_dim);
  auto accesses_list = GetProjectedAccesses(einsum, dspace);

  for (auto& accesses : accesses_list)
  {
    const auto dim_involved = isl_map_involves_dims(accesses.get(),
                                                    isl_dim_in,
                                                    dim_idx,
                                                    1);
    if (dim_involved == isl_bool_true)
    {
      return true;
    }
  }
  return false;
}

const std::set<DimensionId>&
FusedWorkloadDependencyAnalyzer::EinsumDimsDirectlyRelevantToTensor(
  EinsumId einsum,
  DataSpaceId dspace
) const
{
  const auto key = std::make_pair(einsum, dspace);
  auto it = directly_relevant_einsum_dim_memo_.find(key);
  if (it != directly_relevant_einsum_dim_memo_.end())
  {
    return it->second;
  }
  else
  {
    std::set<DimensionId> relevant_dims;
    for (const auto einsum_dim : workload_.EinsumOspaceDimensions(einsum))
    {
      if (EinsumDimIsDirectlyRelevantToTensor(einsum, einsum_dim, dspace))
      {
        relevant_dims.insert(einsum_dim);
      }
    }

    directly_relevant_einsum_dim_memo_.emplace_hint(
      it,
      std::make_pair(key, std::move(relevant_dims))
    );

    return directly_relevant_einsum_dim_memo_.at(key);
  }
}

const std::set<DimensionId>&
FusedWorkloadDependencyAnalyzer::EinsumDimsRelevantToTensor(
  EinsumId einsum,
  DataSpaceId dspace
) const
{
  const auto key = std::make_pair(einsum, dspace);
  auto it = relevant_einsum_dim_memo_.find(key);
  if (it != relevant_einsum_dim_memo_.end())
  {
    return it->second;
  }
  else
  {
    std::set<DimensionId> relevant_dims;
    for (const auto einsum_dim : workload_.EinsumOspaceDimensions(einsum))
    {
      if (EinsumDimIsRelevantToTensor(einsum, einsum_dim, dspace))
      {
        relevant_dims.insert(einsum_dim);
      }
    }

    relevant_einsum_dim_memo_.emplace_hint(
      it,
      std::make_pair(key, std::move(relevant_dims))
    );

    return relevant_einsum_dim_memo_.at(key);
  }
}


const std::set<DimensionId>&
FusedWorkloadDependencyAnalyzer::EquivalentDimensions(EinsumId einsum,
                                                      DimensionId einsum_dim) const
{
  auto it = equivalent_dim_memo_.find(einsum_dim);
  if (it != equivalent_dim_memo_.end())
  {
    return it->second;
  }

  auto equivalent_ranks = std::set<DimensionId>();
  const auto dim_idx = workload_.EinsumDimToIdx(einsum).at(einsum_dim);
  for (auto [dst, _] : workload_.EinsumIdToName())
  {
    auto iterations = GetProjectedIterations(einsum, dst);
    for (auto [iteration, other_einsum] : iterations)
    {
      if (iteration.n_basic_map() != 1)
      {
        continue;
      }

      auto p_projected_map = iteration.copy();
      auto n_dim_out = isl_map_dim(p_projected_map, isl_dim_out);
      auto to_project_after = n_dim_out - dim_idx - 1;
      auto to_project_before = dim_idx;

      if (to_project_after > 0)
      {
        p_projected_map = isl_map_project_out(p_projected_map,
                                              isl_dim_out,
                                              dim_idx + 1,
                                              to_project_after);
      }
      if (to_project_before > 0)
      {
        p_projected_map = isl_map_project_out(p_projected_map,
                                              isl_dim_out,
                                              0,
                                              to_project_before);
      }

      if (!isl_map_is_single_valued(p_projected_map))
      {
        continue;
      }

      auto p_pw_multi_aff = isl_pw_multi_aff_from_map(p_projected_map);
      auto p_multi_pw_aff = isl_pw_multi_aff_to_multi_pw_aff(p_pw_multi_aff);
      if (isl_multi_pw_aff_size(p_multi_pw_aff) != 1)
      {
        throw std::runtime_error("unreachable");
      }
      auto p_pw_aff = isl_multi_pw_aff_get_at(p_multi_pw_aff, 0);
      if (isl_pw_aff_n_piece(p_pw_aff) != 1)
      {
        continue;
      }

      bool has_equivalent_dim = true;
      bool found_potential_equivalent_dim = false;
      int potential_equivalent_dim = 0;
      auto p_aff = isl_pw_aff_as_aff(p_pw_aff);
      for (int i = 0; i < isl_aff_dim(p_aff, isl_dim_in); ++i)
      {
        auto coef = isl_aff_get_coefficient_val(p_aff, isl_dim_in, i);
        if (isl_val_eq_si(coef, 1) && !found_potential_equivalent_dim)
        {
          potential_equivalent_dim = i;
          found_potential_equivalent_dim = true;
        }
        else if (isl_val_eq_si(coef, 1) && found_potential_equivalent_dim)
        {
          has_equivalent_dim = false;
        }
        else if (!isl_val_eq_si(coef, 0))
        {
          has_equivalent_dim = false;
        }
      }

      if (has_equivalent_dim)
      {
        auto equivalent_rank =
          workload_.EinsumIdxToDim(other_einsum).at(potential_equivalent_dim);
        equivalent_ranks.emplace(equivalent_rank);
      }
    }
  }

  equivalent_dim_memo_[einsum_dim] = equivalent_ranks;
  for (const auto rank : equivalent_ranks)
  {
    if (rank == einsum_dim)
    {
      continue;
    }
    equivalent_dim_memo_[rank] = equivalent_dim_memo_.at(einsum_dim);
  }

  return equivalent_dim_memo_.at(einsum_dim);
}


std::vector<isl::map> FusedWorkloadDependencyAnalyzer::GetProjectedAccesses(
EinsumId einsum,
DataSpaceId dspace
) const
{
// Priorotize `einsum` to check relevancy of `dspace`
std::set<EinsumId> src_einsums;
const auto& cur_einsum_read_tensors = workload_.TensorsReadByEinsum(einsum);
const auto it = cur_einsum_read_tensors.find(dspace);
if (it != cur_einsum_read_tensors.end())
{
  src_einsums.emplace(einsum);
}
if (src_einsums.size() == 0)
{
  const auto& cur_einsum_written_tensors = workload_.TensorsWrittenByEinsum(einsum);
  const auto it = cur_einsum_written_tensors.find(dspace);
  if (it != cur_einsum_written_tensors.end())
  {
    src_einsums.emplace(einsum);
  }
}

if (src_einsums.size() == 0)
{
  const auto& reader_einsums = workload_.ReaderEinsums(dspace);
  src_einsums.insert(reader_einsums.begin(), reader_einsums.end());
}
if (src_einsums.size() == 0)
{
  auto writer_einsum = workload_.WriterEinsum(dspace);
  src_einsums.insert(*writer_einsum);
}

std::vector<isl::map> accesses_list;
for (const auto src : src_einsums)
{
  const auto chains = FindEinsumDependencyChain(src, einsum);

  for (const auto& chain : chains)
  {
    auto accesses = workload_.Accesses(src, dspace);
    auto cur_tensor = dspace;
    for (const auto& e : chain)
    {
      if (e != src)
      {
        accesses = workload_.Accesses(e, cur_tensor).apply_range(accesses);
      }

      if (e == chain.back())
      {
        accesses_list.push_back(accesses);
      }
      else
      {
        const auto cur_intermediate_tensor =
            *workload_.TensorsWrittenByEinsum(e).begin();
        const auto& write_of_cur_intermediate =
            workload_.Accesses(e, cur_intermediate_tensor);
        accesses = write_of_cur_intermediate.reverse().apply_range(accesses);
        cur_tensor = cur_intermediate_tensor;
      }
    }
  }
}

return accesses_list;
}


std::vector<std::pair<isl::map, EinsumId>>
FusedWorkloadDependencyAnalyzer::GetProjectedIterations(
EinsumId src,
EinsumId dst
) const
{
const auto chains = FindEinsumDependencyChain(src, dst);

std::vector<std::pair<isl::map, EinsumId>> projections;
for (const auto& chain : chains)
{
  auto projection = isl::manage(
    isl_map_identity(isl_space_map_from_set(
      isl_set_get_space(workload_.EinsumOspaceBound(src).copy())
    )
  ));
  auto cur_tensor = DataSpaceId();
  for (const auto& e : chain)
  {
    if (e != src)
    {
      projection = workload_.Accesses(e, cur_tensor).apply_range(projection);
    }

    if (e == chain.back())
    {
      projections.push_back(std::make_pair(projection, chain.back()));
    }
    else
    {
      const auto cur_intermediate_tensor =
          *workload_.TensorsWrittenByEinsum(e).begin();
      const auto& write_of_cur_intermediate =
          workload_.Accesses(e, cur_intermediate_tensor);
      projection = write_of_cur_intermediate.reverse().apply_range(projection);
      cur_tensor = cur_intermediate_tensor;
    }
  }
  }

  return projections;
}

}