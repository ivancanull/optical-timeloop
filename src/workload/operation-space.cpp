/* Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "operation-space.hpp"

namespace problem
{

extern std::vector<std::function<Point(const WorkloadConfig*, const OperationPoint&)>> Projectors;
extern std::vector<Projection> Projections;

// ======================================= //
//              OperationSpace             //
// ======================================= //

OperationSpace::OperationSpace(const WorkloadConfig* wc) :
    workload_config_(wc)
{
  for (unsigned space_id = 0; space_id < NumDataSpaces; space_id++)
    data_spaces_.push_back(DataSpace(DataSpaceOrder.at(space_id)));
}

OperationSpace::OperationSpace() :
    OperationSpace(nullptr)
{ }

OperationSpace::OperationSpace(const WorkloadConfig* wc, const OperationPoint& low, const OperationPoint& high, bool inclusive) :
    workload_config_(wc)
{
  for (unsigned space_id = 0; space_id < NumDataSpaces; space_id++)
  {
    auto space_low = Projectors.at(space_id)(workload_config_, low);
    auto space_high = Projectors.at(space_id)(workload_config_, high);
    // Increment the high points by 1 because the AAHR constructor wants
    // an exclusive max point.
    if (inclusive)
      space_high.IncrementAllDimensions();
    data_spaces_.push_back(DataSpace(DataSpaceOrder.at(space_id), space_low, space_high));
  }
}

void OperationSpace::Project(DataSpaceID d,
                             const WorkloadConfig* wc,
                             const OperationPoint& problem_point)
{
  Point data_space_point(DataSpaceOrder[d]);

  for (unsigned data_space_dim = 0; data_space_dim < DataSpaceOrder[d]; data_space_dim++)
  {
    data_space_point[data_space_dim] = 0;
    for (auto& term : Projections[d][data_space_dim])
    {
      Coordinate x = problem_point[term.second];
      // FIXME: somehow "compile" the coefficients down for a given
      // workload config so that we avoid the branch and lookup below.
      if (term.first != NumCoefficients)
        data_space_point[data_space_dim] += (x * wc->getCoefficient(term.first));
      else
        data_space_point[data_space_dim] += x;
    }
  }    
}

void OperationSpace::Reset()
{
  for (auto& d : data_spaces_)
    d.Reset();
}

OperationSpace& OperationSpace::operator += (const OperationSpace& s)
{
  for (unsigned i = 0; i < data_spaces_.size(); i++)
    data_spaces_.at(i) += s.data_spaces_.at(i);

  return (*this);
}

OperationSpace& OperationSpace::operator += (const OperationPoint& p)
{
  for (unsigned i = 0; i < data_spaces_.size(); i++)
    data_spaces_.at(i) += Projectors.at(i)(workload_config_, p);

  return (*this);
}

OperationSpace OperationSpace::operator - (const OperationSpace& p)
{
  OperationSpace retval(workload_config_);

  for (unsigned i = 0; i < data_spaces_.size(); i++)
    retval.data_spaces_.at(i) = data_spaces_.at(i) - p.data_spaces_.at(i);
  
  return retval;
}

PerDataSpace<std::size_t> OperationSpace::GetSizes() const
{
  PerDataSpace<std::size_t> retval;
  
  for (unsigned i = 0; i < data_spaces_.size(); i++)
    retval.at(i) = data_spaces_.at(i).size();

  return retval;
}

std::size_t OperationSpace::GetSize(const int t) const
{
  return data_spaces_.at(t).size();
}

bool OperationSpace::IsEmpty(const int t) const
{
  return data_spaces_.at(t).empty();
}

bool OperationSpace::CheckEquality(const OperationSpace& rhs, const int t) const
{
  return data_spaces_.at(t) == rhs.data_spaces_.at(t);
}

void OperationSpace::PrintSizes()
{
  for (unsigned i = 0; i < data_spaces_.size()-1; i++)
    std::cout << DataSpaceIDToName[i] << " = " << data_spaces_.at(i).size() << ", ";
  std::cout << DataSpaceIDToName[data_spaces_.size()-1] << " = " << data_spaces_.back().size() << std::endl;
}

void OperationSpace::Print() const
{
  for (auto& d : data_spaces_)
    d.Print();
}

void OperationSpace::Print(DataSpaceID pv) const
{
  auto& d = data_spaces_.at(unsigned(pv));
  d.Print();
}

} // namespace problem
