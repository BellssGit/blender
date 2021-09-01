/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "BLI_map.hh"
#include "BLI_set.hh"
#include "BLI_stack.hh"

#include "FN_field.hh"

namespace blender::fn {

/**
 * A map to hold the output variables for each function output or input so they can be reused.
 */
using VariableMap = Map<const FieldSource *, Vector<MFVariable *>>;

/**
 * A map of the computed inputs for all of a field system's inputs, to avoid creating duplicates.
 * Usually virtual arrays are just references, but sometimes they can be heavier as well.
 */
using ComputedInputMap = Map<const MFVariable *, GVArrayPtr>;

static MFVariable &get_field_variable(const GField &field, VariableMap &unique_variables)
{
  if (field.is_input()) {
    const FieldInput &input = dynamic_cast<const FieldInput &>(field.source());
    return *unique_variables.lookup(&input).first();
  }
  const FieldOperation &operation = dynamic_cast<const FieldOperation &>(field.source());
  MutableSpan<MFVariable *> operation_outputs = unique_variables.lookup(&operation);
  return *operation_outputs[field.source_output_index()];
}

static const MFVariable &get_field_variable(const GField &field,
                                            const VariableMap &unique_variables)
{
  if (field.is_input()) {
    const FieldInput &input = dynamic_cast<const FieldInput &>(field.source());
    return *unique_variables.lookup(&input).first();
  }
  const FieldOperation &operation = dynamic_cast<const FieldOperation &>(field.source());
  Span<MFVariable *> operation_outputs = unique_variables.lookup(&operation);
  return *operation_outputs[field.source_output_index()];
}

/**
 * TODO: Merge duplicate input nodes, not just fields pointing to the same FieldInput.
 */
static void add_variables_for_input(const GField &field,
                                    Stack<const GField *> &fields_to_visit,
                                    MFProcedureBuilder &builder,
                                    VariableMap &unique_variables)
{
  fields_to_visit.pop();
  const FieldInput &input = dynamic_cast<const FieldInput &>(field.source());
  MFVariable &variable = builder.add_input_parameter(MFDataType::ForSingle(field.cpp_type()),
                                                     input.debug_name());
  unique_variables.add(&input, {&variable});
}

static void add_variables_for_operation(const GField &field,
                                        Stack<const GField *> &fields_to_visit,
                                        MFProcedureBuilder &builder,
                                        VariableMap &unique_variables)
{
  const FieldOperation &operation = dynamic_cast<const FieldOperation &>(field.source());
  for (const GField &input_field : operation.inputs()) {
    if (!unique_variables.contains(&input_field.source())) {
      /* The field for this input hasn't been handled yet. Handle it now, so that we know all
       * of this field's function inputs already have variables. TODO: Verify that this is the
       * best way to do a depth first traversal. These extra lookups don't seem ideal. */
      fields_to_visit.push(&input_field);
      return;
    }
  }

  fields_to_visit.pop();

  Vector<MFVariable *> inputs;
  Set<MFVariable *> unique_inputs;
  for (const GField &input_field : operation.inputs()) {
    MFVariable &input = get_field_variable(input_field, unique_variables);
    unique_inputs.add(&input);
    inputs.append(&input);
  }

  Vector<MFVariable *> outputs = builder.add_call(operation.multi_function(), inputs);
  Vector<MFVariable *> &unique_outputs = unique_variables.lookup_or_add(&operation, {});
  for (MFVariable *output : outputs) {
    unique_outputs.append(output);
  }
}

static void add_unique_variables(const Span<GField> fields,
                                 MFProcedureBuilder &builder,
                                 VariableMap &unique_variables)
{
  Stack<const GField *> fields_to_visit;
  for (const GField &field : fields) {
    fields_to_visit.push(&field);
  }

  while (!fields_to_visit.is_empty()) {
    const GField &field = *fields_to_visit.peek();
    if (unique_variables.contains(&field.source())) {
      fields_to_visit.pop();
      continue;
    }

    if (field.is_input()) {
      add_variables_for_input(field, fields_to_visit, builder, unique_variables);
    }
    else {
      add_variables_for_operation(field, fields_to_visit, builder, unique_variables);
    }
  }
}

/**
 * Add destruct calls to the procedure so that internal variables and inputs are destructed before
 * the procedure finishes. Currently this just adds all of the destructs at the end. That is not
 * optimal, but properly ordering destructs should be combined with reordering function calls to
 * use variables more optimally.
 */
static void add_destructs(const Span<GField> fields,
                          MFProcedureBuilder &builder,
                          VariableMap &unique_variables)
{
  Set<MFVariable *> destructed_variables;
  Set<MFVariable *> outputs;
  for (const GField &field : fields) {
    /* Currently input fields are handled separately in the evaluator. */
    BLI_assert(!field.is_input());
    outputs.add(&get_field_variable(field, unique_variables));
  }

  for (MutableSpan<MFVariable *> variables : unique_variables.values()) {
    for (MFVariable *variable : variables) {
      /* Don't destruct the variable if it is used as an output parameter. */
      if (!outputs.contains(variable)) {
        builder.add_destruct(*variable);
      }
    }
  }
}

static void build_procedure(const Span<GField> fields,
                            MFProcedure &procedure,
                            VariableMap &unique_variables)
{
  MFProcedureBuilder builder{procedure};

  add_unique_variables(fields, builder, unique_variables);

  add_destructs(fields, builder, unique_variables);

  builder.add_return();

  for (const GField &field : fields) {
    MFVariable &input = get_field_variable(field, unique_variables);
    builder.add_output_parameter(input);
  }

  // std::cout << procedure.to_dot();

  BLI_assert(procedure.validate());
}

/**
 * TODO: Maybe this doesn't add inputs in the same order as the the unique
 * variable traversal. Add a test for that and fix it if it doesn't work.
 */
static void gather_inputs(const Span<GField> fields,
                          const VariableMap &unique_variables,
                          const IndexMask mask,
                          MFParamsBuilder &params,
                          Vector<GVArrayPtr> &r_inputs)
{
  Set<const MFVariable *> computed_inputs;
  Stack<const GField *> fields_to_visit;
  for (const GField &field : fields) {
    fields_to_visit.push(&field);
  }

  while (!fields_to_visit.is_empty()) {
    const GField &field = *fields_to_visit.pop();
    if (field.is_input()) {
      const FieldInput &input = dynamic_cast<const FieldInput &>(field.source());
      const MFVariable &variable = get_field_variable(field, unique_variables);
      if (!computed_inputs.contains(&variable)) {
        GVArrayPtr data = input.get_varray_generic_context(mask);
        computed_inputs.add_new(&variable);
        params.add_readonly_single_input(*data, input.debug_name());
        r_inputs.append(std::move(data));
      }
    }
    else {
      const FieldOperation &operation = dynamic_cast<const FieldOperation &>(field.source());
      for (const GField &input_field : operation.inputs()) {
        fields_to_visit.push(&input_field);
      }
    }
  }
}

static void add_outputs(MFParamsBuilder &params, Span<GMutableSpan> outputs)
{
  for (const int i : outputs.index_range()) {
    params.add_uninitialized_single_output(outputs[i]);
  }
}

static void evaluate_non_input_fields(const Span<GField> fields,
                                      const IndexMask mask,
                                      const Span<GMutableSpan> outputs)
{
  MFProcedure procedure;
  VariableMap unique_variables;
  build_procedure(fields, procedure, unique_variables);

  MFProcedureExecutor executor{"Evaluate Field", procedure};
  MFParamsBuilder params{executor, mask.min_array_size()};
  MFContextBuilder context;

  Vector<GVArrayPtr> inputs;
  gather_inputs(fields, unique_variables, mask, params, inputs);

  add_outputs(params, outputs);

  executor.call(mask, params, context);
}

/**
 * Evaluate more than one prodecure at a time, since often intermediate results will be shared
 * between multiple final results, and the procedure evaluator can optimize for this case.
 */
void evaluate_fields(const Span<GField> fields,
                     const IndexMask mask,
                     const Span<GMutableSpan> outputs)
{
  BLI_assert(fields.size() == outputs.size());

  /* Process fields that just connect to inputs separately, since otherwise we need a special
   * case to avoid sharing the same variable for an input and output parameters elsewhere.
   * TODO: It would be nice if there were a more elegant way to handle this, rather than a
   * separate step here, but I haven't thought of anything yet. */
  Vector<GField> non_input_fields{fields};
  Vector<GMutableSpan> non_input_outputs{outputs};
  for (int i = fields.size() - 1; i >= 0; i--) {
    if (non_input_fields[i].is_input()) {
      dynamic_cast<const FieldInput &>(non_input_fields[i].source())
          .get_varray_generic_context(mask)
          ->materialize(mask, outputs[i].data());

      non_input_fields.remove_and_reorder(i);
      non_input_outputs.remove_and_reorder(i);
    }
  }

  if (!non_input_fields.is_empty()) {
    evaluate_non_input_fields(non_input_fields, mask, non_input_outputs);
  }
}

/**
 * #r_value is expected to be uninitialized.
 */
void evaluate_constant_field(const GField &field, void *r_value)
{
  GMutableSpan value_span{field.cpp_type(), r_value, 1};
  evaluate_fields({field}, IndexRange(1), {value_span});
}

}  // namespace blender::fn
