/*
 * Copyright (c) 2021-2023, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2021, Gunnar Beutner <gbeutner@serenityos.org>
 * Copyright (c) 2021, Marcin Gasperowicz <xnooga@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Find.h>
#include <LibJS/AST.h>
#include <LibJS/Bytecode/Generator.h>
#include <LibJS/Bytecode/Instruction.h>
#include <LibJS/Bytecode/Op.h>
#include <LibJS/Bytecode/Register.h>
#include <LibJS/Bytecode/StringTable.h>
#include <LibJS/Runtime/Environment.h>
#include <LibJS/Runtime/ErrorTypes.h>

namespace JS {

Bytecode::CodeGenerationErrorOr<void> ASTNode::generate_bytecode(Bytecode::Generator&) const
{
    return Bytecode::CodeGenerationError {
        this,
        "Missing generate_bytecode()"sv,
    };
}

Bytecode::CodeGenerationErrorOr<void> ScopeNode::generate_bytecode(Bytecode::Generator& generator) const
{
    // Note: SwitchStatement has its own codegen, but still calls into this function to handle the scoping of the switch body.
    auto is_switch_statement = is<SwitchStatement>(*this);
    bool did_create_lexical_environment = false;

    if (is<BlockStatement>(*this) || is_switch_statement) {
        if (has_lexical_declarations()) {
            generator.block_declaration_instantiation(*this);
            did_create_lexical_environment = true;
        }
        if (is_switch_statement)
            return {};
    } else if (is<Program>(*this)) {
        // GlobalDeclarationInstantiation is handled by the C++ AO.
    } else {
        // FunctionDeclarationInstantiation is handled by the C++ AO.
    }

    for (auto& child : children()) {
        TRY(child->generate_bytecode(generator));
        if (generator.is_current_block_terminated())
            break;
    }

    if (did_create_lexical_environment)
        generator.end_variable_scope();

    return {};
}

Bytecode::CodeGenerationErrorOr<void> EmptyStatement::generate_bytecode(Bytecode::Generator&) const
{
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ExpressionStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    return m_expression->generate_bytecode(generator);
}

Bytecode::CodeGenerationErrorOr<void> BinaryExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(m_lhs->generate_bytecode(generator));
    auto lhs_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(lhs_reg);

    TRY(m_rhs->generate_bytecode(generator));

    switch (m_op) {
    case BinaryOp::Addition:
        generator.emit<Bytecode::Op::Add>(lhs_reg);
        break;
    case BinaryOp::Subtraction:
        generator.emit<Bytecode::Op::Sub>(lhs_reg);
        break;
    case BinaryOp::Multiplication:
        generator.emit<Bytecode::Op::Mul>(lhs_reg);
        break;
    case BinaryOp::Division:
        generator.emit<Bytecode::Op::Div>(lhs_reg);
        break;
    case BinaryOp::Modulo:
        generator.emit<Bytecode::Op::Mod>(lhs_reg);
        break;
    case BinaryOp::Exponentiation:
        generator.emit<Bytecode::Op::Exp>(lhs_reg);
        break;
    case BinaryOp::GreaterThan:
        generator.emit<Bytecode::Op::GreaterThan>(lhs_reg);
        break;
    case BinaryOp::GreaterThanEquals:
        generator.emit<Bytecode::Op::GreaterThanEquals>(lhs_reg);
        break;
    case BinaryOp::LessThan:
        generator.emit<Bytecode::Op::LessThan>(lhs_reg);
        break;
    case BinaryOp::LessThanEquals:
        generator.emit<Bytecode::Op::LessThanEquals>(lhs_reg);
        break;
    case BinaryOp::LooselyInequals:
        generator.emit<Bytecode::Op::LooselyInequals>(lhs_reg);
        break;
    case BinaryOp::LooselyEquals:
        generator.emit<Bytecode::Op::LooselyEquals>(lhs_reg);
        break;
    case BinaryOp::StrictlyInequals:
        generator.emit<Bytecode::Op::StrictlyInequals>(lhs_reg);
        break;
    case BinaryOp::StrictlyEquals:
        generator.emit<Bytecode::Op::StrictlyEquals>(lhs_reg);
        break;
    case BinaryOp::BitwiseAnd:
        generator.emit<Bytecode::Op::BitwiseAnd>(lhs_reg);
        break;
    case BinaryOp::BitwiseOr:
        generator.emit<Bytecode::Op::BitwiseOr>(lhs_reg);
        break;
    case BinaryOp::BitwiseXor:
        generator.emit<Bytecode::Op::BitwiseXor>(lhs_reg);
        break;
    case BinaryOp::LeftShift:
        generator.emit<Bytecode::Op::LeftShift>(lhs_reg);
        break;
    case BinaryOp::RightShift:
        generator.emit<Bytecode::Op::RightShift>(lhs_reg);
        break;
    case BinaryOp::UnsignedRightShift:
        generator.emit<Bytecode::Op::UnsignedRightShift>(lhs_reg);
        break;
    case BinaryOp::In:
        generator.emit<Bytecode::Op::In>(lhs_reg);
        break;
    case BinaryOp::InstanceOf:
        generator.emit<Bytecode::Op::InstanceOf>(lhs_reg);
        break;
    default:
        VERIFY_NOT_REACHED();
    }
    return {};
}

Bytecode::CodeGenerationErrorOr<void> LogicalExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(m_lhs->generate_bytecode(generator));

    // lhs
    // jump op (true) end (false) rhs
    // rhs
    // jump always (true) end
    // end

    auto& rhs_block = generator.make_block();
    auto& end_block = generator.make_block();

    switch (m_op) {
    case LogicalOp::And:
        generator.emit<Bytecode::Op::JumpConditional>().set_targets(
            Bytecode::Label { rhs_block },
            Bytecode::Label { end_block });
        break;
    case LogicalOp::Or:
        generator.emit<Bytecode::Op::JumpConditional>().set_targets(
            Bytecode::Label { end_block },
            Bytecode::Label { rhs_block });
        break;
    case LogicalOp::NullishCoalescing:
        generator.emit<Bytecode::Op::JumpNullish>().set_targets(
            Bytecode::Label { rhs_block },
            Bytecode::Label { end_block });
        break;
    default:
        VERIFY_NOT_REACHED();
    }

    generator.switch_to_basic_block(rhs_block);
    TRY(m_rhs->generate_bytecode(generator));

    generator.emit<Bytecode::Op::Jump>().set_targets(
        Bytecode::Label { end_block },
        {});

    generator.switch_to_basic_block(end_block);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> UnaryExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    if (m_op == UnaryOp::Delete)
        return generator.emit_delete_reference(m_lhs);

    // Typeof needs some special handling for when the LHS is an Identifier. Namely, it shouldn't throw on unresolvable references, but instead return "undefined".
    if (m_op != UnaryOp::Typeof)
        TRY(m_lhs->generate_bytecode(generator));

    switch (m_op) {
    case UnaryOp::BitwiseNot:
        generator.emit<Bytecode::Op::BitwiseNot>();
        break;
    case UnaryOp::Not:
        generator.emit<Bytecode::Op::Not>();
        break;
    case UnaryOp::Plus:
        generator.emit<Bytecode::Op::UnaryPlus>();
        break;
    case UnaryOp::Minus:
        generator.emit<Bytecode::Op::UnaryMinus>();
        break;
    case UnaryOp::Typeof:
        if (is<Identifier>(*m_lhs)) {
            auto& identifier = static_cast<Identifier const&>(*m_lhs);
            generator.emit<Bytecode::Op::TypeofVariable>(generator.intern_identifier(identifier.string()));
            break;
        }

        TRY(m_lhs->generate_bytecode(generator));
        generator.emit<Bytecode::Op::Typeof>();
        break;
    case UnaryOp::Void:
        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
        break;
    case UnaryOp::Delete: // Delete is implemented above.
    default:
        VERIFY_NOT_REACHED();
    }

    return {};
}

Bytecode::CodeGenerationErrorOr<void> NumericLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::LoadImmediate>(m_value);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> BooleanLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::LoadImmediate>(Value(m_value));
    return {};
}

Bytecode::CodeGenerationErrorOr<void> NullLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::LoadImmediate>(js_null());
    return {};
}

Bytecode::CodeGenerationErrorOr<void> BigIntLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    // 1. Return the NumericValue of NumericLiteral as defined in 12.8.3.
    auto integer = [&] {
        if (m_value[0] == '0' && m_value.length() >= 3)
            if (m_value[1] == 'x' || m_value[1] == 'X')
                return Crypto::SignedBigInteger::from_base(16, m_value.substring(2, m_value.length() - 3));
        if (m_value[1] == 'o' || m_value[1] == 'O')
            return Crypto::SignedBigInteger::from_base(8, m_value.substring(2, m_value.length() - 3));
        if (m_value[1] == 'b' || m_value[1] == 'B')
            return Crypto::SignedBigInteger::from_base(2, m_value.substring(2, m_value.length() - 3));
        return Crypto::SignedBigInteger::from_base(10, m_value.substring(0, m_value.length() - 1));
    }();

    generator.emit<Bytecode::Op::NewBigInt>(integer);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> StringLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::NewString>(generator.intern_string(m_value));
    return {};
}

Bytecode::CodeGenerationErrorOr<void> RegExpLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    auto source_index = generator.intern_string(m_pattern);
    auto flags_index = generator.intern_string(m_flags);
    generator.emit<Bytecode::Op::NewRegExp>(source_index, flags_index);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> Identifier::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::GetVariable>(generator.intern_identifier(m_string));
    return {};
}

static Bytecode::CodeGenerationErrorOr<void> arguments_to_array_for_call(Bytecode::Generator& generator, ReadonlySpan<CallExpression::Argument> arguments)
{

    if (arguments.is_empty()) {
        generator.emit<Bytecode::Op::NewArray>();
        return {};
    }

    auto first_spread = find_if(arguments.begin(), arguments.end(), [](auto el) { return el.is_spread; });

    Bytecode::Register args_start_reg { 0 };
    for (auto it = arguments.begin(); it != first_spread; ++it) {
        auto reg = generator.allocate_register();
        if (args_start_reg.index() == 0)
            args_start_reg = reg;
    }
    u32 i = 0;
    for (auto it = arguments.begin(); it != first_spread; ++it, ++i) {
        VERIFY(it->is_spread == false);
        Bytecode::Register reg { args_start_reg.index() + i };
        TRY(it->value->generate_bytecode(generator));
        generator.emit<Bytecode::Op::Store>(reg);
    }

    if (first_spread.index() != 0)
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2u, AK::Array { args_start_reg, Bytecode::Register { args_start_reg.index() + static_cast<u32>(first_spread.index() - 1) } });
    else
        generator.emit<Bytecode::Op::NewArray>();

    if (first_spread != arguments.end()) {
        auto array_reg = generator.allocate_register();
        generator.emit<Bytecode::Op::Store>(array_reg);
        for (auto it = first_spread; it != arguments.end(); ++it) {
            TRY(it->value->generate_bytecode(generator));
            generator.emit<Bytecode::Op::Append>(array_reg, it->is_spread);
        }
        generator.emit<Bytecode::Op::Load>(array_reg);
    }

    return {};
}

Bytecode::CodeGenerationErrorOr<void> SuperCall::generate_bytecode(Bytecode::Generator& generator) const
{
    if (m_is_synthetic == IsPartOfSyntheticConstructor::Yes) {
        // NOTE: This is the case where we have a fake constructor(...args) { super(...args); } which
        //       shouldn't call @@iterator of %Array.prototype%.
        VERIFY(m_arguments.size() == 1);
        VERIFY(m_arguments[0].is_spread);
        auto const& argument = m_arguments[0];
        // This generates a single argument, which will be implicitly passed in accumulator
        MUST(argument.value->generate_bytecode(generator));
    } else {
        TRY(arguments_to_array_for_call(generator, m_arguments));
    }

    generator.emit<Bytecode::Op::SuperCall>(m_is_synthetic == IsPartOfSyntheticConstructor::Yes);

    return {};
}

static Bytecode::CodeGenerationErrorOr<void> generate_binding_pattern_bytecode(Bytecode::Generator& generator, BindingPattern const& pattern, Bytecode::Op::SetVariable::InitializationMode, Bytecode::Register const& value_reg);

Bytecode::CodeGenerationErrorOr<void> AssignmentExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    if (m_op == AssignmentOp::Assignment) {
        // AssignmentExpression : LeftHandSideExpression = AssignmentExpression
        return m_lhs.visit(
            // 1. If LeftHandSideExpression is neither an ObjectLiteral nor an ArrayLiteral, then
            [&](NonnullRefPtr<Expression const> const& lhs) -> Bytecode::CodeGenerationErrorOr<void> {
                // a. Let lref be the result of evaluating LeftHandSideExpression.
                // b. ReturnIfAbrupt(lref).
                Optional<Bytecode::Register> base_object_register;
                Optional<Bytecode::Register> computed_property_register;

                if (is<MemberExpression>(*lhs)) {
                    auto& expression = static_cast<MemberExpression const&>(*lhs);
                    TRY(expression.object().generate_bytecode(generator));

                    base_object_register = generator.allocate_register();
                    generator.emit<Bytecode::Op::Store>(*base_object_register);

                    if (expression.is_computed()) {
                        TRY(expression.property().generate_bytecode(generator));
                        computed_property_register = generator.allocate_register();
                        generator.emit<Bytecode::Op::Store>(*computed_property_register);

                        // To be continued later with PutByValue.
                    } else if (expression.property().is_identifier()) {
                        // Do nothing, this will be handled by PutById later.
                    } else {
                        return Bytecode::CodeGenerationError {
                            &expression,
                            "Unimplemented non-computed member expression"sv
                        };
                    }
                } else if (is<Identifier>(*lhs)) {
                    // NOTE: For Identifiers, we cannot perform GetVariable and then write into the reference it retrieves, only SetVariable can do this.
                    // FIXME: However, this breaks spec as we are doing variable lookup after evaluating the RHS. This is observable in an object environment, where we visibly perform HasOwnProperty and Get(@@unscopables) on the binded object.
                } else {
                    TRY(lhs->generate_bytecode(generator));
                }

                // FIXME: c. If IsAnonymousFunctionDefinition(AssignmentExpression) and IsIdentifierRef of LeftHandSideExpression are both true, then
                //           i. Let rval be ? NamedEvaluation of AssignmentExpression with argument lref.[[ReferencedName]].

                // d. Else,
                // i. Let rref be the result of evaluating AssignmentExpression.
                // ii. Let rval be ? GetValue(rref).
                TRY(m_rhs->generate_bytecode(generator));

                // e. Perform ? PutValue(lref, rval).
                if (is<Identifier>(*lhs)) {
                    auto& identifier = static_cast<Identifier const&>(*lhs);
                    generator.emit<Bytecode::Op::SetVariable>(generator.intern_identifier(identifier.string()));
                } else if (is<MemberExpression>(*lhs)) {
                    auto& expression = static_cast<MemberExpression const&>(*lhs);

                    if (expression.is_computed()) {
                        generator.emit<Bytecode::Op::PutByValue>(*base_object_register, *computed_property_register);
                    } else if (expression.property().is_identifier()) {
                        auto identifier_table_ref = generator.intern_identifier(verify_cast<Identifier>(expression.property()).string());
                        generator.emit<Bytecode::Op::PutById>(*base_object_register, identifier_table_ref);
                    } else {
                        return Bytecode::CodeGenerationError {
                            &expression,
                            "Unimplemented non-computed member expression"sv
                        };
                    }
                } else {
                    return Bytecode::CodeGenerationError {
                        lhs,
                        "Unimplemented/invalid node used a reference"sv
                    };
                }

                // f. Return rval.
                // NOTE: This is already in the accumulator.
                return {};
            },
            // 2. Let assignmentPattern be the AssignmentPattern that is covered by LeftHandSideExpression.
            [&](NonnullRefPtr<BindingPattern const> const& pattern) -> Bytecode::CodeGenerationErrorOr<void> {
                // 3. Let rref be the result of evaluating AssignmentExpression.
                // 4. Let rval be ? GetValue(rref).
                TRY(m_rhs->generate_bytecode(generator));
                auto value_register = generator.allocate_register();
                generator.emit<Bytecode::Op::Store>(value_register);

                // 5. Perform ? DestructuringAssignmentEvaluation of assignmentPattern with argument rval.
                TRY(generate_binding_pattern_bytecode(generator, pattern, Bytecode::Op::SetVariable::InitializationMode::Set, value_register));

                // 6. Return rval.
                generator.emit<Bytecode::Op::Load>(value_register);
                return {};
            });
    }

    VERIFY(m_lhs.has<NonnullRefPtr<Expression const>>());
    auto& lhs = m_lhs.get<NonnullRefPtr<Expression const>>();

    TRY(generator.emit_load_from_reference(lhs));

    Bytecode::BasicBlock* rhs_block_ptr { nullptr };
    Bytecode::BasicBlock* end_block_ptr { nullptr };

    // Logical assignments short circuit.
    if (m_op == AssignmentOp::AndAssignment) { // &&=
        rhs_block_ptr = &generator.make_block();
        end_block_ptr = &generator.make_block();

        generator.emit<Bytecode::Op::JumpConditional>().set_targets(
            Bytecode::Label { *rhs_block_ptr },
            Bytecode::Label { *end_block_ptr });
    } else if (m_op == AssignmentOp::OrAssignment) { // ||=
        rhs_block_ptr = &generator.make_block();
        end_block_ptr = &generator.make_block();

        generator.emit<Bytecode::Op::JumpConditional>().set_targets(
            Bytecode::Label { *end_block_ptr },
            Bytecode::Label { *rhs_block_ptr });
    } else if (m_op == AssignmentOp::NullishAssignment) { // ??=
        rhs_block_ptr = &generator.make_block();
        end_block_ptr = &generator.make_block();

        generator.emit<Bytecode::Op::JumpNullish>().set_targets(
            Bytecode::Label { *rhs_block_ptr },
            Bytecode::Label { *end_block_ptr });
    }

    if (rhs_block_ptr)
        generator.switch_to_basic_block(*rhs_block_ptr);

    // lhs_reg is a part of the rhs_block because the store isn't necessary
    // if the logical assignment condition fails.
    auto lhs_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(lhs_reg);
    TRY(m_rhs->generate_bytecode(generator));

    switch (m_op) {
    case AssignmentOp::AdditionAssignment:
        generator.emit<Bytecode::Op::Add>(lhs_reg);
        break;
    case AssignmentOp::SubtractionAssignment:
        generator.emit<Bytecode::Op::Sub>(lhs_reg);
        break;
    case AssignmentOp::MultiplicationAssignment:
        generator.emit<Bytecode::Op::Mul>(lhs_reg);
        break;
    case AssignmentOp::DivisionAssignment:
        generator.emit<Bytecode::Op::Div>(lhs_reg);
        break;
    case AssignmentOp::ModuloAssignment:
        generator.emit<Bytecode::Op::Mod>(lhs_reg);
        break;
    case AssignmentOp::ExponentiationAssignment:
        generator.emit<Bytecode::Op::Exp>(lhs_reg);
        break;
    case AssignmentOp::BitwiseAndAssignment:
        generator.emit<Bytecode::Op::BitwiseAnd>(lhs_reg);
        break;
    case AssignmentOp::BitwiseOrAssignment:
        generator.emit<Bytecode::Op::BitwiseOr>(lhs_reg);
        break;
    case AssignmentOp::BitwiseXorAssignment:
        generator.emit<Bytecode::Op::BitwiseXor>(lhs_reg);
        break;
    case AssignmentOp::LeftShiftAssignment:
        generator.emit<Bytecode::Op::LeftShift>(lhs_reg);
        break;
    case AssignmentOp::RightShiftAssignment:
        generator.emit<Bytecode::Op::RightShift>(lhs_reg);
        break;
    case AssignmentOp::UnsignedRightShiftAssignment:
        generator.emit<Bytecode::Op::UnsignedRightShift>(lhs_reg);
        break;
    case AssignmentOp::AndAssignment:
    case AssignmentOp::OrAssignment:
    case AssignmentOp::NullishAssignment:
        break; // These are handled above.
    default:
        return Bytecode::CodeGenerationError {
            this,
            "Unimplemented operation"sv,
        };
    }

    TRY(generator.emit_store_to_reference(lhs));

    if (end_block_ptr) {
        generator.emit<Bytecode::Op::Jump>().set_targets(
            Bytecode::Label { *end_block_ptr },
            {});

        generator.switch_to_basic_block(*end_block_ptr);
    }

    return {};
}

// 14.13.3 Runtime Semantics: Evaluation, https://tc39.es/ecma262/#sec-labelled-statements-runtime-semantics-evaluation
//  LabelledStatement : LabelIdentifier : LabelledItem
Bytecode::CodeGenerationErrorOr<void> LabelledStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    // Return ? LabelledEvaluation of this LabelledStatement with argument « ».
    return generate_labelled_evaluation(generator, {});
}

// 14.13.4 Runtime Semantics: LabelledEvaluation, https://tc39.es/ecma262/#sec-runtime-semantics-labelledevaluation
// LabelledStatement : LabelIdentifier : LabelledItem
Bytecode::CodeGenerationErrorOr<void> LabelledStatement::generate_labelled_evaluation(Bytecode::Generator& generator, Vector<DeprecatedFlyString> const& label_set) const
{
    // Convert the m_labelled_item NNRP to a reference early so we don't have to do it every single time we want to use it.
    auto const& labelled_item = *m_labelled_item;

    // 1. Let label be the StringValue of LabelIdentifier.
    // NOTE: Not necessary, this is m_label.

    // 2. Let newLabelSet be the list-concatenation of labelSet and « label ».
    // FIXME: Avoid copy here.
    auto new_label_set = label_set;
    new_label_set.append(m_label);

    // 3. Let stmtResult be LabelledEvaluation of LabelledItem with argument newLabelSet.
    // NOTE: stmtResult will be in the accumulator after running the generated bytecode.
    if (is<IterationStatement>(labelled_item)) {
        auto const& iteration_statement = static_cast<IterationStatement const&>(labelled_item);
        TRY(iteration_statement.generate_labelled_evaluation(generator, new_label_set));
    } else if (is<SwitchStatement>(labelled_item)) {
        auto const& switch_statement = static_cast<SwitchStatement const&>(labelled_item);
        TRY(switch_statement.generate_labelled_evaluation(generator, new_label_set));
    } else if (is<LabelledStatement>(labelled_item)) {
        auto const& labelled_statement = static_cast<LabelledStatement const&>(labelled_item);
        TRY(labelled_statement.generate_labelled_evaluation(generator, new_label_set));
    } else {
        auto& labelled_break_block = generator.make_block();

        // NOTE: We do not need a continuable scope as `continue;` is not allowed outside of iteration statements, throwing a SyntaxError in the parser.
        generator.begin_breakable_scope(Bytecode::Label { labelled_break_block }, new_label_set);
        TRY(labelled_item.generate_bytecode(generator));
        generator.end_breakable_scope();

        if (!generator.is_current_block_terminated()) {
            generator.emit<Bytecode::Op::Jump>().set_targets(
                Bytecode::Label { labelled_break_block },
                {});
        }

        generator.switch_to_basic_block(labelled_break_block);
    }

    // 4. If stmtResult.[[Type]] is break and SameValue(stmtResult.[[Target]], label) is true, then
    //    a. Set stmtResult to NormalCompletion(stmtResult.[[Value]]).
    // NOTE: These steps are performed by making labelled break jump straight to the appropriate break block, which preserves the statement result's value in the accumulator.

    // 5. Return Completion(stmtResult).
    // NOTE: This is in the accumulator.
    return {};
}

Bytecode::CodeGenerationErrorOr<void> IterationStatement::generate_labelled_evaluation(Bytecode::Generator&, Vector<DeprecatedFlyString> const&) const
{
    return Bytecode::CodeGenerationError {
        this,
        "Missing generate_labelled_evaluation()"sv,
    };
}

Bytecode::CodeGenerationErrorOr<void> WhileStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    return generate_labelled_evaluation(generator, {});
}

Bytecode::CodeGenerationErrorOr<void> WhileStatement::generate_labelled_evaluation(Bytecode::Generator& generator, Vector<DeprecatedFlyString> const& label_set) const
{
    // test
    // jump if_false (true) end (false) body
    // body
    // jump always (true) test
    // end
    auto& test_block = generator.make_block();
    auto& body_block = generator.make_block();
    auto& end_block = generator.make_block();

    // Init result register
    generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
    auto result_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(result_reg);

    // jump to the test block
    generator.emit<Bytecode::Op::Jump>().set_targets(
        Bytecode::Label { test_block },
        {});

    generator.switch_to_basic_block(test_block);
    TRY(m_test->generate_bytecode(generator));
    generator.emit<Bytecode::Op::JumpConditional>().set_targets(
        Bytecode::Label { body_block },
        Bytecode::Label { end_block });

    generator.switch_to_basic_block(body_block);
    generator.begin_continuable_scope(Bytecode::Label { test_block }, label_set);
    generator.begin_breakable_scope(Bytecode::Label { end_block }, label_set);
    TRY(m_body->generate_bytecode(generator));
    generator.end_breakable_scope();
    generator.end_continuable_scope();

    if (!generator.is_current_block_terminated()) {
        generator.emit<Bytecode::Op::Jump>().set_targets(
            Bytecode::Label { test_block },
            {});
    }

    generator.switch_to_basic_block(end_block);
    generator.emit<Bytecode::Op::Load>(result_reg);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> DoWhileStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    return generate_labelled_evaluation(generator, {});
}

Bytecode::CodeGenerationErrorOr<void> DoWhileStatement::generate_labelled_evaluation(Bytecode::Generator& generator, Vector<DeprecatedFlyString> const& label_set) const
{
    // jump always (true) body
    // test
    // jump if_false (true) end (false) body
    // body
    // jump always (true) test
    // end
    auto& test_block = generator.make_block();
    auto& body_block = generator.make_block();
    auto& end_block = generator.make_block();

    // Init result register
    generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
    auto result_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(result_reg);

    // jump to the body block
    generator.emit<Bytecode::Op::Jump>().set_targets(
        Bytecode::Label { body_block },
        {});

    generator.switch_to_basic_block(test_block);
    TRY(m_test->generate_bytecode(generator));
    generator.emit<Bytecode::Op::JumpConditional>().set_targets(
        Bytecode::Label { body_block },
        Bytecode::Label { end_block });

    generator.switch_to_basic_block(body_block);
    generator.begin_continuable_scope(Bytecode::Label { test_block }, label_set);
    generator.begin_breakable_scope(Bytecode::Label { end_block }, label_set);
    TRY(m_body->generate_bytecode(generator));
    generator.end_breakable_scope();
    generator.end_continuable_scope();

    if (!generator.is_current_block_terminated()) {
        generator.emit<Bytecode::Op::Jump>().set_targets(
            Bytecode::Label { test_block },
            {});
    }

    generator.switch_to_basic_block(end_block);
    generator.emit<Bytecode::Op::Load>(result_reg);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ForStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    return generate_labelled_evaluation(generator, {});
}

Bytecode::CodeGenerationErrorOr<void> ForStatement::generate_labelled_evaluation(Bytecode::Generator& generator, Vector<DeprecatedFlyString> const& label_set) const
{
    // init
    // jump always (true) test
    // test
    // jump if_true (true) body (false) end
    // body
    // jump always (true) update
    // update
    // jump always (true) test
    // end

    // If 'test' is missing, fuse the 'test' and 'body' basic blocks
    // If 'update' is missing, fuse the 'body' and 'update' basic blocks

    Bytecode::BasicBlock* test_block_ptr { nullptr };
    Bytecode::BasicBlock* body_block_ptr { nullptr };
    Bytecode::BasicBlock* update_block_ptr { nullptr };

    auto& end_block = generator.make_block();

    bool has_lexical_environment = false;

    if (m_init) {
        if (m_init->is_variable_declaration()) {
            auto& variable_declaration = verify_cast<VariableDeclaration>(*m_init);

            if (variable_declaration.is_lexical_declaration()) {
                has_lexical_environment = true;

                // FIXME: Is Block correct?
                generator.begin_variable_scope();

                bool is_const = variable_declaration.is_constant_declaration();
                // NOTE: Nothing in the callback throws an exception.
                MUST(variable_declaration.for_each_bound_name([&](auto const& name) {
                    auto index = generator.intern_identifier(name);
                    generator.emit<Bytecode::Op::CreateVariable>(index, Bytecode::Op::EnvironmentMode::Lexical, is_const);
                }));
            }
        }

        TRY(m_init->generate_bytecode(generator));
    }

    body_block_ptr = &generator.make_block();

    if (m_test)
        test_block_ptr = &generator.make_block();
    else
        test_block_ptr = body_block_ptr;

    if (m_update)
        update_block_ptr = &generator.make_block();
    else
        update_block_ptr = body_block_ptr;

    generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
    auto result_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(result_reg);

    generator.emit<Bytecode::Op::Jump>().set_targets(
        Bytecode::Label { *test_block_ptr },
        {});

    if (m_test) {
        generator.switch_to_basic_block(*test_block_ptr);
        TRY(m_test->generate_bytecode(generator));
        generator.emit<Bytecode::Op::JumpConditional>().set_targets(
            Bytecode::Label { *body_block_ptr },
            Bytecode::Label { end_block });
    }

    if (m_update) {
        generator.switch_to_basic_block(*update_block_ptr);
        TRY(m_update->generate_bytecode(generator));
        generator.emit<Bytecode::Op::Jump>().set_targets(
            Bytecode::Label { *test_block_ptr },
            {});
    }

    generator.switch_to_basic_block(*body_block_ptr);
    generator.begin_continuable_scope(Bytecode::Label { m_update ? *update_block_ptr : *test_block_ptr }, label_set);
    generator.begin_breakable_scope(Bytecode::Label { end_block }, label_set);
    TRY(m_body->generate_bytecode(generator));
    generator.end_breakable_scope();
    generator.end_continuable_scope();

    if (!generator.is_current_block_terminated()) {
        if (m_update) {
            generator.emit<Bytecode::Op::Jump>().set_targets(
                Bytecode::Label { *update_block_ptr },
                {});
        } else {
            generator.emit<Bytecode::Op::Jump>().set_targets(
                Bytecode::Label { *test_block_ptr },
                {});
        }
    }

    generator.switch_to_basic_block(end_block);
    generator.emit<Bytecode::Op::Load>(result_reg);

    if (has_lexical_environment)
        generator.end_variable_scope();

    return {};
}

Bytecode::CodeGenerationErrorOr<void> ObjectExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::NewObject>();
    if (m_properties.is_empty())
        return {};

    auto object_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(object_reg);

    generator.push_home_object(object_reg);

    for (auto& property : m_properties) {
        Bytecode::Op::PropertyKind property_kind;
        switch (property->type()) {
        case ObjectProperty::Type::KeyValue:
            property_kind = Bytecode::Op::PropertyKind::KeyValue;
            break;
        case ObjectProperty::Type::Getter:
            property_kind = Bytecode::Op::PropertyKind::Getter;
            break;
        case ObjectProperty::Type::Setter:
            property_kind = Bytecode::Op::PropertyKind::Setter;
            break;
        case ObjectProperty::Type::Spread:
            property_kind = Bytecode::Op::PropertyKind::Spread;
            break;
        case ObjectProperty::Type::ProtoSetter:
            property_kind = Bytecode::Op::PropertyKind::ProtoSetter;
            break;
        }

        if (is<StringLiteral>(property->key())) {
            auto& string_literal = static_cast<StringLiteral const&>(property->key());
            Bytecode::IdentifierTableIndex key_name = generator.intern_identifier(string_literal.value());

            if (property_kind != Bytecode::Op::PropertyKind::Spread)
                TRY(property->value().generate_bytecode(generator));

            generator.emit<Bytecode::Op::PutById>(object_reg, key_name, property_kind);
        } else {
            TRY(property->key().generate_bytecode(generator));
            auto property_reg = generator.allocate_register();
            generator.emit<Bytecode::Op::Store>(property_reg);

            if (property_kind != Bytecode::Op::PropertyKind::Spread)
                TRY(property->value().generate_bytecode(generator));

            generator.emit<Bytecode::Op::PutByValue>(object_reg, property_reg, property_kind);
        }
    }

    generator.emit<Bytecode::Op::Load>(object_reg);

    generator.pop_home_object();
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ArrayExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    if (m_elements.is_empty()) {
        generator.emit<Bytecode::Op::NewArray>();
        return {};
    }

    auto first_spread = find_if(m_elements.begin(), m_elements.end(), [](auto el) { return el && is<SpreadExpression>(*el); });

    Bytecode::Register args_start_reg { 0 };
    for (auto it = m_elements.begin(); it != first_spread; ++it) {
        auto reg = generator.allocate_register();
        if (args_start_reg.index() == 0)
            args_start_reg = reg;
    }
    u32 i = 0;
    for (auto it = m_elements.begin(); it != first_spread; ++it, ++i) {
        Bytecode::Register reg { args_start_reg.index() + i };
        if (!*it)
            generator.emit<Bytecode::Op::LoadImmediate>(Value {});
        else {
            TRY((*it)->generate_bytecode(generator));
        }
        generator.emit<Bytecode::Op::Store>(reg);
    }

    if (first_spread.index() != 0)
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2u, AK::Array { args_start_reg, Bytecode::Register { args_start_reg.index() + static_cast<u32>(first_spread.index() - 1) } });
    else
        generator.emit<Bytecode::Op::NewArray>();

    if (first_spread != m_elements.end()) {
        auto array_reg = generator.allocate_register();
        generator.emit<Bytecode::Op::Store>(array_reg);
        for (auto it = first_spread; it != m_elements.end(); ++it) {
            if (!*it) {
                generator.emit<Bytecode::Op::LoadImmediate>(Value {});
                generator.emit<Bytecode::Op::Append>(array_reg, false);
            } else {
                TRY((*it)->generate_bytecode(generator));
                generator.emit<Bytecode::Op::Append>(array_reg, *it && is<SpreadExpression>(**it));
            }
        }
        generator.emit<Bytecode::Op::Load>(array_reg);
    }

    return {};
}

Bytecode::CodeGenerationErrorOr<void> MemberExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    return generator.emit_load_from_reference(*this);
}

Bytecode::CodeGenerationErrorOr<void> FunctionDeclaration::generate_bytecode(Bytecode::Generator& generator) const
{
    if (m_is_hoisted) {
        auto index = generator.intern_identifier(name());
        generator.emit<Bytecode::Op::GetVariable>(index);
        generator.emit<Bytecode::Op::SetVariable>(index, Bytecode::Op::SetVariable::InitializationMode::Set, Bytecode::Op::EnvironmentMode::Var);
    }
    return {};
}

Bytecode::CodeGenerationErrorOr<void> FunctionExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    bool has_name = !name().is_empty();
    Optional<Bytecode::IdentifierTableIndex> name_identifier;

    if (has_name) {
        generator.begin_variable_scope();

        name_identifier = generator.intern_identifier(name());
        generator.emit<Bytecode::Op::CreateVariable>(*name_identifier, Bytecode::Op::EnvironmentMode::Lexical, true);
    }

    generator.emit_new_function(*this);

    if (has_name) {
        generator.emit<Bytecode::Op::SetVariable>(*name_identifier, Bytecode::Op::SetVariable::InitializationMode::Initialize, Bytecode::Op::EnvironmentMode::Lexical);
        generator.end_variable_scope();
    }

    return {};
}

static Bytecode::CodeGenerationErrorOr<void> generate_object_binding_pattern_bytecode(Bytecode::Generator& generator, BindingPattern const& pattern, Bytecode::Op::SetVariable::InitializationMode initialization_mode, Bytecode::Register const& value_reg)
{
    Vector<Bytecode::Register> excluded_property_names;
    auto has_rest = false;
    if (pattern.entries.size() > 0)
        has_rest = pattern.entries[pattern.entries.size() - 1].is_rest;

    for (auto& [name, alias, initializer, is_rest] : pattern.entries) {
        if (is_rest) {
            VERIFY(name.has<NonnullRefPtr<Identifier const>>());
            VERIFY(alias.has<Empty>());
            VERIFY(!initializer);

            auto identifier = name.get<NonnullRefPtr<Identifier const>>()->string();
            auto interned_identifier = generator.intern_identifier(identifier);

            generator.emit_with_extra_register_slots<Bytecode::Op::CopyObjectExcludingProperties>(excluded_property_names.size(), value_reg, excluded_property_names);
            generator.emit<Bytecode::Op::SetVariable>(interned_identifier, initialization_mode);

            return {};
        }

        Bytecode::StringTableIndex name_index;

        if (name.has<NonnullRefPtr<Identifier const>>()) {
            auto identifier = name.get<NonnullRefPtr<Identifier const>>()->string();
            name_index = generator.intern_string(identifier);

            if (has_rest) {
                auto excluded_name_reg = generator.allocate_register();
                excluded_property_names.append(excluded_name_reg);
                generator.emit<Bytecode::Op::NewString>(name_index);
                generator.emit<Bytecode::Op::Store>(excluded_name_reg);
            }

            generator.emit<Bytecode::Op::Load>(value_reg);
            generator.emit<Bytecode::Op::GetById>(generator.intern_identifier(identifier));
        } else {
            auto expression = name.get<NonnullRefPtr<Expression const>>();
            TRY(expression->generate_bytecode(generator));

            if (has_rest) {
                auto excluded_name_reg = generator.allocate_register();
                excluded_property_names.append(excluded_name_reg);
                generator.emit<Bytecode::Op::Store>(excluded_name_reg);
            }

            generator.emit<Bytecode::Op::GetByValue>(value_reg);
        }

        if (initializer) {
            auto& if_undefined_block = generator.make_block();
            auto& if_not_undefined_block = generator.make_block();

            generator.emit<Bytecode::Op::JumpUndefined>().set_targets(
                Bytecode::Label { if_undefined_block },
                Bytecode::Label { if_not_undefined_block });

            generator.switch_to_basic_block(if_undefined_block);
            TRY(initializer->generate_bytecode(generator));
            generator.emit<Bytecode::Op::Jump>().set_targets(
                Bytecode::Label { if_not_undefined_block },
                {});

            generator.switch_to_basic_block(if_not_undefined_block);
        }

        if (alias.has<NonnullRefPtr<BindingPattern const>>()) {
            auto& binding_pattern = *alias.get<NonnullRefPtr<BindingPattern const>>();
            auto nested_value_reg = generator.allocate_register();
            generator.emit<Bytecode::Op::Store>(nested_value_reg);
            TRY(generate_binding_pattern_bytecode(generator, binding_pattern, initialization_mode, nested_value_reg));
        } else if (alias.has<Empty>()) {
            if (name.has<NonnullRefPtr<Expression const>>()) {
                // This needs some sort of SetVariableByValue opcode, as it's a runtime binding
                return Bytecode::CodeGenerationError {
                    name.get<NonnullRefPtr<Expression const>>().ptr(),
                    "Unimplemented name/alias pair: Empty/Expression"sv,
                };
            }

            auto& identifier = name.get<NonnullRefPtr<Identifier const>>()->string();
            generator.emit<Bytecode::Op::SetVariable>(generator.intern_identifier(identifier), initialization_mode);
        } else {
            auto& identifier = alias.get<NonnullRefPtr<Identifier const>>()->string();
            generator.emit<Bytecode::Op::SetVariable>(generator.intern_identifier(identifier), initialization_mode);
        }
    }
    return {};
}

static Bytecode::CodeGenerationErrorOr<void> generate_array_binding_pattern_bytecode(Bytecode::Generator& generator, BindingPattern const& pattern, Bytecode::Op::SetVariable::InitializationMode initialization_mode, Bytecode::Register const& value_reg)
{
    /*
     * Consider the following destructuring assignment:
     *
     *     let [a, b, c, d, e] = o;
     *
     * It would be fairly trivial to just loop through this iterator, getting the value
     * at each step and assigning them to the binding sequentially. However, this is not
     * correct: once an iterator is exhausted, it must not be called again. This complicates
     * the bytecode. In order to accomplish this, we do the following:
     *
     * - Reserve a special boolean register which holds 'true' if the iterator is exhausted,
     *   and false otherwise
     * - When we are retrieving the value which should be bound, we first check this register.
     *   If it is 'true', we load undefined into the accumulator. Otherwise, we grab the next
     *   value from the iterator and store it into the accumulator.
     *
     * Note that the is_exhausted register does not need to be loaded with false because the
     * first IteratorNext bytecode is _not_ proceeded by an exhausted check, as it is
     * unnecessary.
     */

    auto is_iterator_exhausted_register = generator.allocate_register();

    auto iterator_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Load>(value_reg);
    generator.emit<Bytecode::Op::GetIterator>();
    generator.emit<Bytecode::Op::Store>(iterator_reg);
    bool first = true;

    auto temp_iterator_result_reg = generator.allocate_register();

    auto assign_accumulator_to_alias = [&](auto& alias) {
        return alias.visit(
            [&](Empty) -> Bytecode::CodeGenerationErrorOr<void> {
                // This element is an elision
                return {};
            },
            [&](NonnullRefPtr<Identifier const> const& identifier) -> Bytecode::CodeGenerationErrorOr<void> {
                auto interned_index = generator.intern_identifier(identifier->string());
                generator.emit<Bytecode::Op::SetVariable>(interned_index, initialization_mode);
                return {};
            },
            [&](NonnullRefPtr<BindingPattern const> const& pattern) -> Bytecode::CodeGenerationErrorOr<void> {
                // Store the accumulator value in a permanent register
                auto target_reg = generator.allocate_register();
                generator.emit<Bytecode::Op::Store>(target_reg);
                return generate_binding_pattern_bytecode(generator, pattern, initialization_mode, target_reg);
            },
            [&](NonnullRefPtr<MemberExpression const> const& expr) -> Bytecode::CodeGenerationErrorOr<void> {
                return generator.emit_store_to_reference(*expr);
            });
    };

    for (auto& [name, alias, initializer, is_rest] : pattern.entries) {
        VERIFY(name.has<Empty>());

        if (is_rest) {
            VERIFY(!initializer);

            if (first) {
                // The iterator has not been called, and is thus known to be not exhausted
                generator.emit<Bytecode::Op::Load>(iterator_reg);
                generator.emit<Bytecode::Op::IteratorToArray>();
            } else {
                auto& if_exhausted_block = generator.make_block();
                auto& if_not_exhausted_block = generator.make_block();
                auto& continuation_block = generator.make_block();

                generator.emit<Bytecode::Op::Load>(is_iterator_exhausted_register);
                generator.emit<Bytecode::Op::JumpConditional>().set_targets(
                    Bytecode::Label { if_exhausted_block },
                    Bytecode::Label { if_not_exhausted_block });

                generator.switch_to_basic_block(if_exhausted_block);
                generator.emit<Bytecode::Op::NewArray>();
                generator.emit<Bytecode::Op::Jump>().set_targets(
                    Bytecode::Label { continuation_block },
                    {});

                generator.switch_to_basic_block(if_not_exhausted_block);
                generator.emit<Bytecode::Op::Load>(iterator_reg);
                generator.emit<Bytecode::Op::IteratorToArray>();
                generator.emit<Bytecode::Op::Jump>().set_targets(
                    Bytecode::Label { continuation_block },
                    {});

                generator.switch_to_basic_block(continuation_block);
            }

            return assign_accumulator_to_alias(alias);
        }

        // In the first iteration of the loop, a few things are true which can save
        // us some bytecode:
        //  - the iterator result is still in the accumulator, so we can avoid a load
        //  - the iterator is not yet exhausted, which can save us a jump and some
        //    creation

        auto& iterator_is_exhausted_block = generator.make_block();

        if (!first) {
            auto& iterator_is_not_exhausted_block = generator.make_block();

            generator.emit<Bytecode::Op::Load>(is_iterator_exhausted_register);
            generator.emit<Bytecode::Op::JumpConditional>().set_targets(
                Bytecode::Label { iterator_is_exhausted_block },
                Bytecode::Label { iterator_is_not_exhausted_block });

            generator.switch_to_basic_block(iterator_is_not_exhausted_block);
            generator.emit<Bytecode::Op::Load>(iterator_reg);
        }

        generator.emit<Bytecode::Op::IteratorNext>();
        generator.emit<Bytecode::Op::Store>(temp_iterator_result_reg);
        generator.emit<Bytecode::Op::IteratorResultDone>();
        generator.emit<Bytecode::Op::Store>(is_iterator_exhausted_register);

        // We still have to check for exhaustion here. If the iterator is exhausted,
        // we need to bail before trying to get the value
        auto& no_bail_block = generator.make_block();
        generator.emit<Bytecode::Op::JumpConditional>().set_targets(
            Bytecode::Label { iterator_is_exhausted_block },
            Bytecode::Label { no_bail_block });

        generator.switch_to_basic_block(no_bail_block);

        // Get the next value in the iterator
        generator.emit<Bytecode::Op::Load>(temp_iterator_result_reg);
        generator.emit<Bytecode::Op::IteratorResultValue>();

        auto& create_binding_block = generator.make_block();
        generator.emit<Bytecode::Op::Jump>().set_targets(
            Bytecode::Label { create_binding_block },
            {});

        // The iterator is exhausted, so we just load undefined and continue binding
        generator.switch_to_basic_block(iterator_is_exhausted_block);
        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
        generator.emit<Bytecode::Op::Jump>().set_targets(
            Bytecode::Label { create_binding_block },
            {});

        // Create the actual binding. The value which this entry must bind is now in the
        // accumulator. We can proceed, processing the alias as a nested  destructuring
        // pattern if necessary.
        generator.switch_to_basic_block(create_binding_block);

        if (initializer) {
            auto& value_is_undefined_block = generator.make_block();
            auto& value_is_not_undefined_block = generator.make_block();

            generator.emit<Bytecode::Op::JumpUndefined>().set_targets(
                Bytecode::Label { value_is_undefined_block },
                Bytecode::Label { value_is_not_undefined_block });

            generator.switch_to_basic_block(value_is_undefined_block);
            TRY(initializer->generate_bytecode(generator));
            generator.emit<Bytecode::Op::Jump>(Bytecode::Label { value_is_not_undefined_block });

            generator.switch_to_basic_block(value_is_not_undefined_block);
        }

        TRY(assign_accumulator_to_alias(alias));

        first = false;
    }

    return {};
}

static Bytecode::CodeGenerationErrorOr<void> generate_binding_pattern_bytecode(Bytecode::Generator& generator, BindingPattern const& pattern, Bytecode::Op::SetVariable::InitializationMode initialization_mode, Bytecode::Register const& value_reg)
{
    if (pattern.kind == BindingPattern::Kind::Object)
        return generate_object_binding_pattern_bytecode(generator, pattern, initialization_mode, value_reg);

    return generate_array_binding_pattern_bytecode(generator, pattern, initialization_mode, value_reg);
}

static Bytecode::CodeGenerationErrorOr<void> assign_accumulator_to_variable_declarator(Bytecode::Generator& generator, VariableDeclarator const& declarator, VariableDeclaration const& declaration)
{
    auto initialization_mode = declaration.is_lexical_declaration() ? Bytecode::Op::SetVariable::InitializationMode::Initialize : Bytecode::Op::SetVariable::InitializationMode::Set;

    return declarator.target().visit(
        [&](NonnullRefPtr<Identifier const> const& id) -> Bytecode::CodeGenerationErrorOr<void> {
            generator.emit<Bytecode::Op::SetVariable>(generator.intern_identifier(id->string()), initialization_mode);
            return {};
        },
        [&](NonnullRefPtr<BindingPattern const> const& pattern) -> Bytecode::CodeGenerationErrorOr<void> {
            auto value_register = generator.allocate_register();
            generator.emit<Bytecode::Op::Store>(value_register);
            return generate_binding_pattern_bytecode(generator, pattern, initialization_mode, value_register);
        });
}

Bytecode::CodeGenerationErrorOr<void> VariableDeclaration::generate_bytecode(Bytecode::Generator& generator) const
{
    for (auto& declarator : m_declarations) {
        if (declarator->init()) {
            TRY(declarator->init()->generate_bytecode(generator));
            TRY(assign_accumulator_to_variable_declarator(generator, declarator, *this));
        } else if (m_declaration_kind != DeclarationKind::Var) {
            generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
            TRY(assign_accumulator_to_variable_declarator(generator, declarator, *this));
        }
    }

    return {};
}

Bytecode::CodeGenerationErrorOr<void> CallExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    auto callee_reg = generator.allocate_register();
    auto this_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
    generator.emit<Bytecode::Op::Store>(this_reg);

    if (is<NewExpression>(this)) {
        TRY(m_callee->generate_bytecode(generator));
        generator.emit<Bytecode::Op::Store>(callee_reg);
    } else if (is<MemberExpression>(*m_callee)) {
        auto& member_expression = static_cast<MemberExpression const&>(*m_callee);

        // https://tc39.es/ecma262/#sec-super-keyword-runtime-semantics-evaluation
        if (is<SuperExpression>(member_expression.object())) {
            // 1. Let env be GetThisEnvironment().
            // 2. Let actualThis be ? env.GetThisBinding().
            generator.emit<Bytecode::Op::ResolveThisBinding>();
            generator.emit<Bytecode::Op::Store>(this_reg);

            Optional<Bytecode::Register> computed_property_value_register;

            if (member_expression.is_computed()) {
                // SuperProperty : super [ Expression ]
                // 3. Let propertyNameReference be ? Evaluation of Expression.
                // 4. Let propertyNameValue be ? GetValue(propertyNameReference).
                TRY(member_expression.property().generate_bytecode(generator));
                computed_property_value_register = generator.allocate_register();
                generator.emit<Bytecode::Op::Store>(*computed_property_value_register);
            }

            // 5/7. Return ? MakeSuperPropertyReference(actualThis, propertyKey, strict).

            // https://tc39.es/ecma262/#sec-makesuperpropertyreference
            // 1. Let env be GetThisEnvironment().
            // 2. Assert: env.HasSuperBinding() is true.
            // 3. Let baseValue be ? env.GetSuperBase().
            generator.emit<Bytecode::Op::ResolveSuperBase>();

            // 4. Return the Reference Record { [[Base]]: baseValue, [[ReferencedName]]: propertyKey, [[Strict]]: strict, [[ThisValue]]: actualThis }.
            if (computed_property_value_register.has_value()) {
                // 5. Let propertyKey be ? ToPropertyKey(propertyNameValue).
                // FIXME: This does ToPropertyKey out of order, which is observable by Symbol.toPrimitive!
                generator.emit<Bytecode::Op::GetByValue>(*computed_property_value_register);
            } else {
                // 3. Let propertyKey be StringValue of IdentifierName.
                auto identifier_table_ref = generator.intern_identifier(verify_cast<Identifier>(member_expression.property()).string());
                generator.emit<Bytecode::Op::GetById>(identifier_table_ref);
            }
        } else {
            TRY(member_expression.object().generate_bytecode(generator));
            generator.emit<Bytecode::Op::Store>(this_reg);
            if (member_expression.is_computed()) {
                TRY(member_expression.property().generate_bytecode(generator));
                generator.emit<Bytecode::Op::GetByValue>(this_reg);
            } else {
                auto identifier_table_ref = [&] {
                    if (is<PrivateIdentifier>(member_expression.property()))
                        return generator.intern_identifier(verify_cast<PrivateIdentifier>(member_expression.property()).string());
                    return generator.intern_identifier(verify_cast<Identifier>(member_expression.property()).string());
                }();

                generator.emit<Bytecode::Op::GetById>(identifier_table_ref);
            }
        }

        generator.emit<Bytecode::Op::Store>(callee_reg);
    } else {
        // FIXME: this = global object in sloppy mode.
        TRY(m_callee->generate_bytecode(generator));
        generator.emit<Bytecode::Op::Store>(callee_reg);
    }

    TRY(arguments_to_array_for_call(generator, arguments()));

    Bytecode::Op::Call::CallType call_type;
    if (is<NewExpression>(*this)) {
        call_type = Bytecode::Op::Call::CallType::Construct;
    } else if (m_callee->is_identifier() && static_cast<Identifier const&>(*m_callee).string() == "eval"sv) {
        call_type = Bytecode::Op::Call::CallType::DirectEval;
    } else {
        call_type = Bytecode::Op::Call::CallType::Call;
    }

    Optional<Bytecode::StringTableIndex> expression_string_index;
    if (auto expression_string = this->expression_string(); expression_string.has_value())
        expression_string_index = generator.intern_string(expression_string.release_value());

    generator.emit<Bytecode::Op::Call>(call_type, callee_reg, this_reg, expression_string_index);

    return {};
}

Bytecode::CodeGenerationErrorOr<void> ReturnStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    if (m_argument)
        TRY(m_argument->generate_bytecode(generator));
    else
        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());

    if (generator.is_in_generator_or_async_function()) {
        generator.perform_needed_unwinds<Bytecode::Op::Yield>();
        generator.emit<Bytecode::Op::Yield>(nullptr);
    } else {
        generator.perform_needed_unwinds<Bytecode::Op::Return>();
        generator.emit<Bytecode::Op::Return>();
    }

    return {};
}

Bytecode::CodeGenerationErrorOr<void> YieldExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    VERIFY(generator.is_in_generator_function());

    auto received_completion_register = generator.allocate_register();
    auto received_completion_type_register = generator.allocate_register();
    auto received_completion_value_register = generator.allocate_register();

    auto type_identifier = generator.intern_identifier("type");
    auto value_identifier = generator.intern_identifier("value");

    auto get_received_completion_type_and_value = [&]() {
        // The accumulator is set to an object, for example: { "type": 1 (normal), value: 1337 }
        generator.emit<Bytecode::Op::Store>(received_completion_register);

        generator.emit<Bytecode::Op::GetById>(type_identifier);
        generator.emit<Bytecode::Op::Store>(received_completion_type_register);

        generator.emit<Bytecode::Op::Load>(received_completion_register);
        generator.emit<Bytecode::Op::GetById>(value_identifier);
        generator.emit<Bytecode::Op::Store>(received_completion_value_register);
    };

    if (m_is_yield_from) {
        // 15.5.5 Runtime Semantics: Evaluation, https://tc39.es/ecma262/#sec-generator-function-definitions-runtime-semantics-evaluation
        // FIXME: 1. Let generatorKind be GetGeneratorKind().

        // 2. Let exprRef be ? Evaluation of AssignmentExpression.
        // 3. Let value be ? GetValue(exprRef).
        VERIFY(m_argument);
        TRY(m_argument->generate_bytecode(generator));

        // 4. Let iteratorRecord be ? GetIterator(value, generatorKind).
        // FIXME: Consider generatorKind.
        auto iterator_record_register = generator.allocate_register();
        generator.emit<Bytecode::Op::GetIterator>();
        generator.emit<Bytecode::Op::Store>(iterator_record_register);

        // 5. Let iterator be iteratorRecord.[[Iterator]].
        auto iterator_register = generator.allocate_register();
        auto iterator_identifier = generator.intern_identifier("iterator");
        generator.emit<Bytecode::Op::GetById>(iterator_identifier);
        generator.emit<Bytecode::Op::Store>(iterator_register);

        // Cache iteratorRecord.[[NextMethod]] for use in step 7.a.i.
        auto next_method_register = generator.allocate_register();
        auto next_method_identifier = generator.intern_identifier("next");
        generator.emit<Bytecode::Op::Load>(iterator_record_register);
        generator.emit<Bytecode::Op::GetById>(next_method_identifier);
        generator.emit<Bytecode::Op::Store>(next_method_register);

        // 6. Let received be NormalCompletion(undefined).
        // See get_received_completion_type_and_value above.
        generator.emit<Bytecode::Op::LoadImmediate>(Value(to_underlying(Completion::Type::Normal)));
        generator.emit<Bytecode::Op::Store>(received_completion_type_register);

        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
        generator.emit<Bytecode::Op::Store>(received_completion_value_register);

        // 7. Repeat,
        auto& loop_block = generator.make_block();
        auto& continuation_block = generator.make_block();
        auto& loop_end_block = generator.make_block();

        generator.emit<Bytecode::Op::Jump>(Bytecode::Label { loop_block });
        generator.switch_to_basic_block(loop_block);

        // a. If received.[[Type]] is normal, then
        auto& type_is_normal_block = generator.make_block();
        auto& is_type_throw_block = generator.make_block();

        generator.emit<Bytecode::Op::LoadImmediate>(Value(to_underlying(Completion::Type::Normal)));
        generator.emit<Bytecode::Op::StrictlyEquals>(received_completion_type_register);
        generator.emit<Bytecode::Op::JumpConditional>(
            Bytecode::Label { type_is_normal_block },
            Bytecode::Label { is_type_throw_block });

        generator.switch_to_basic_block(type_is_normal_block);

        // i. Let innerResult be ? Call(iteratorRecord.[[NextMethod]], iteratorRecord.[[Iterator]], « received.[[Value]] »).
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2, AK::Array { received_completion_value_register, received_completion_value_register });
        generator.emit<Bytecode::Op::Call>(Bytecode::Op::Call::CallType::Call, next_method_register, iterator_register);

        // FIXME: ii. If generatorKind is async, set innerResult to ? Await(innerResult).

        // iii. If innerResult is not an Object, throw a TypeError exception.
        generator.emit<Bytecode::Op::ThrowIfNotObject>();

        auto inner_result_register = generator.allocate_register();
        generator.emit<Bytecode::Op::Store>(inner_result_register);

        // iv. Let done be ? IteratorComplete(innerResult).
        generator.emit<Bytecode::Op::IteratorResultDone>();

        // v. If done is true, then
        auto& type_is_normal_done_block = generator.make_block();
        auto& type_is_normal_not_done_block = generator.make_block();
        generator.emit<Bytecode::Op::JumpConditional>(
            Bytecode::Label { type_is_normal_done_block },
            Bytecode::Label { type_is_normal_not_done_block });

        generator.switch_to_basic_block(type_is_normal_done_block);

        // 1. Return ? IteratorValue(innerResult).
        generator.emit<Bytecode::Op::Load>(inner_result_register);
        generator.emit<Bytecode::Op::IteratorResultValue>();
        generator.emit<Bytecode::Op::Jump>(Bytecode::Label { loop_end_block });

        generator.switch_to_basic_block(type_is_normal_not_done_block);

        // FIXME: vi. If generatorKind is async, set received to Completion(AsyncGeneratorYield(? IteratorValue(innerResult))).
        // vii. Else, set received to Completion(GeneratorYield(innerResult)).
        // FIXME: Else,
        generator.emit<Bytecode::Op::Load>(inner_result_register);

        // FIXME: Yield currently only accepts a Value, not an object conforming to the IteratorResult interface, so we have to do an observable lookup of `value` here.
        generator.emit<Bytecode::Op::IteratorResultValue>();

        generator.emit<Bytecode::Op::Yield>(Bytecode::Label { continuation_block });

        // b. Else if received.[[Type]] is throw, then
        generator.switch_to_basic_block(is_type_throw_block);
        auto& type_is_throw_block = generator.make_block();
        auto& type_is_return_block = generator.make_block();

        generator.emit<Bytecode::Op::LoadImmediate>(Value(to_underlying(Completion::Type::Throw)));
        generator.emit<Bytecode::Op::StrictlyEquals>(received_completion_type_register);
        generator.emit<Bytecode::Op::JumpConditional>(
            Bytecode::Label { type_is_throw_block },
            Bytecode::Label { type_is_return_block });

        generator.switch_to_basic_block(type_is_throw_block);

        // i. Let throw be ? GetMethod(iterator, "throw").
        auto throw_method_register = generator.allocate_register();
        auto throw_identifier = generator.intern_identifier("throw");
        generator.emit<Bytecode::Op::Load>(iterator_register);
        generator.emit<Bytecode::Op::GetMethod>(throw_identifier);
        generator.emit<Bytecode::Op::Store>(throw_method_register);

        // ii. If throw is not undefined, then
        auto& throw_method_is_defined_block = generator.make_block();
        auto& throw_method_is_undefined_block = generator.make_block();
        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
        generator.emit<Bytecode::Op::StrictlyInequals>(throw_method_register);
        generator.emit<Bytecode::Op::JumpConditional>(
            Bytecode::Label { throw_method_is_defined_block },
            Bytecode::Label { throw_method_is_undefined_block });

        generator.switch_to_basic_block(throw_method_is_defined_block);

        // 1. Let innerResult be ? Call(throw, iterator, « received.[[Value]] »).
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2, AK::Array { received_completion_value_register, received_completion_value_register });
        generator.emit<Bytecode::Op::Call>(Bytecode::Op::Call::CallType::Call, throw_method_register, iterator_register);

        // FIXME: 2. If generatorKind is async, set innerResult to ? Await(innerResult).

        // 3. NOTE: Exceptions from the inner iterator throw method are propagated. Normal completions from an inner throw method are processed similarly to an inner next.
        // 4. If innerResult is not an Object, throw a TypeError exception.
        generator.emit<Bytecode::Op::ThrowIfNotObject>();
        generator.emit<Bytecode::Op::Store>(inner_result_register);

        // 5. Let done be ? IteratorComplete(innerResult).
        generator.emit<Bytecode::Op::IteratorResultDone>();

        // 6. If done is true, then
        auto& type_is_throw_done_block = generator.make_block();
        auto& type_is_throw_not_done_block = generator.make_block();
        generator.emit<Bytecode::Op::JumpConditional>(
            Bytecode::Label { type_is_throw_done_block },
            Bytecode::Label { type_is_throw_not_done_block });

        generator.switch_to_basic_block(type_is_throw_done_block);

        // a. Return ? IteratorValue(innerResult).
        generator.emit<Bytecode::Op::Load>(inner_result_register);
        generator.emit<Bytecode::Op::IteratorResultValue>();
        generator.emit<Bytecode::Op::Jump>(Bytecode::Label { loop_end_block });

        generator.switch_to_basic_block(type_is_throw_not_done_block);

        // FIXME: 7. If generatorKind is async, set received to Completion(AsyncGeneratorYield(? IteratorValue(innerResult))).
        // 8. Else, set received to Completion(GeneratorYield(innerResult)).
        // FIXME: Else,
        generator.emit<Bytecode::Op::Load>(inner_result_register);

        // FIXME: Yield currently only accepts a Value, not an object conforming to the IteratorResult interface, so we have to do an observable lookup of `value` here.
        generator.emit<Bytecode::Op::IteratorResultValue>();

        generator.emit<Bytecode::Op::Yield>(Bytecode::Label { continuation_block });

        generator.switch_to_basic_block(throw_method_is_undefined_block);

        // 1. NOTE: If iterator does not have a throw method, this throw is going to terminate the yield* loop. But first we need to give iterator a chance to clean up.

        // 2. Let closeCompletion be Completion Record { [[Type]]: normal, [[Value]]: empty, [[Target]]: empty }.
        // FIXME: 3. If generatorKind is async, perform ? AsyncIteratorClose(iteratorRecord, closeCompletion).
        // 4. Else, perform ? IteratorClose(iteratorRecord, closeCompletion).
        // FIXME: Else,
        generator.emit<Bytecode::Op::Load>(iterator_record_register);
        generator.emit<Bytecode::Op::IteratorClose>(Completion::Type::Normal, Optional<Value> {});

        // 5. NOTE: The next step throws a TypeError to indicate that there was a yield* protocol violation: iterator does not have a throw method.
        // 6. Throw a TypeError exception.
        generator.emit<Bytecode::Op::NewTypeError>(generator.intern_string(ErrorType::YieldFromIteratorMissingThrowMethod.message()));
        generator.perform_needed_unwinds<Bytecode::Op::Throw>();
        generator.emit<Bytecode::Op::Throw>();

        // c. Else,
        // i. Assert: received.[[Type]] is return.
        generator.switch_to_basic_block(type_is_return_block);

        // ii. Let return be ? GetMethod(iterator, "return").
        auto return_method_register = generator.allocate_register();
        auto return_identifier = generator.intern_identifier("return");
        generator.emit<Bytecode::Op::Load>(iterator_register);
        generator.emit<Bytecode::Op::GetMethod>(return_identifier);
        generator.emit<Bytecode::Op::Store>(return_method_register);

        // iii. If return is undefined, then
        auto& return_is_undefined_block = generator.make_block();
        auto& return_is_defined_block = generator.make_block();
        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
        generator.emit<Bytecode::Op::StrictlyEquals>(return_method_register);
        generator.emit<Bytecode::Op::JumpConditional>(
            Bytecode::Label { return_is_undefined_block },
            Bytecode::Label { return_is_defined_block });

        generator.switch_to_basic_block(return_is_undefined_block);

        // FIXME: 1. If generatorKind is async, set received.[[Value]] to ? Await(received.[[Value]]).
        // 2. Return ? received.
        // NOTE: This will always be a return completion.
        generator.emit<Bytecode::Op::Load>(received_completion_value_register);
        generator.perform_needed_unwinds<Bytecode::Op::Yield>();
        generator.emit<Bytecode::Op::Yield>(nullptr);

        generator.switch_to_basic_block(return_is_defined_block);

        // iv. Let innerReturnResult be ? Call(return, iterator, « received.[[Value]] »).
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2, AK::Array { received_completion_value_register, received_completion_value_register });
        generator.emit<Bytecode::Op::Call>(Bytecode::Op::Call::CallType::Call, return_method_register, iterator_register);

        // FIXME: v. If generatorKind is async, set innerReturnResult to ? Await(innerReturnResult).

        // vi. If innerReturnResult is not an Object, throw a TypeError exception.
        generator.emit<Bytecode::Op::ThrowIfNotObject>();

        auto inner_return_result_register = generator.allocate_register();
        generator.emit<Bytecode::Op::Store>(inner_return_result_register);

        // vii. Let done be ? IteratorComplete(innerReturnResult).
        generator.emit<Bytecode::Op::IteratorResultDone>();

        // viii. If done is true, then
        auto& type_is_return_done_block = generator.make_block();
        auto& type_is_return_not_done_block = generator.make_block();
        generator.emit<Bytecode::Op::JumpConditional>(
            Bytecode::Label { type_is_return_done_block },
            Bytecode::Label { type_is_return_not_done_block });

        generator.switch_to_basic_block(type_is_return_done_block);

        // 1. Let value be ? IteratorValue(innerReturnResult).
        generator.emit<Bytecode::Op::Load>(inner_result_register);
        generator.emit<Bytecode::Op::IteratorResultValue>();

        // 2. Return Completion Record { [[Type]]: return, [[Value]]: value, [[Target]]: empty }.
        generator.perform_needed_unwinds<Bytecode::Op::Yield>();
        generator.emit<Bytecode::Op::Yield>(nullptr);

        generator.switch_to_basic_block(type_is_return_not_done_block);

        // FIXME: ix. If generatorKind is async, set received to Completion(AsyncGeneratorYield(? IteratorValue(innerReturnResult))).
        // x. Else, set received to Completion(GeneratorYield(innerReturnResult)).
        // FIXME: Else,
        generator.emit<Bytecode::Op::Load>(inner_return_result_register);

        // FIXME: Yield currently only accepts a Value, not an object conforming to the IteratorResult interface, so we have to do an observable lookup of `value` here.
        generator.emit<Bytecode::Op::IteratorResultValue>();

        generator.emit<Bytecode::Op::Yield>(Bytecode::Label { continuation_block });

        generator.switch_to_basic_block(continuation_block);
        get_received_completion_type_and_value();
        generator.emit<Bytecode::Op::Jump>(Bytecode::Label { loop_block });

        generator.switch_to_basic_block(loop_end_block);
        return {};
    }

    if (m_argument)
        TRY(m_argument->generate_bytecode(generator));
    else
        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());

    auto& continuation_block = generator.make_block();
    generator.emit<Bytecode::Op::Yield>(Bytecode::Label { continuation_block });
    generator.switch_to_basic_block(continuation_block);
    get_received_completion_type_and_value();

    auto& normal_completion_continuation_block = generator.make_block();
    auto& throw_completion_continuation_block = generator.make_block();

    generator.emit<Bytecode::Op::LoadImmediate>(Value(to_underlying(Completion::Type::Normal)));
    generator.emit<Bytecode::Op::StrictlyEquals>(received_completion_type_register);
    generator.emit<Bytecode::Op::JumpConditional>(
        Bytecode::Label { normal_completion_continuation_block },
        Bytecode::Label { throw_completion_continuation_block });

    auto& throw_value_block = generator.make_block();
    auto& return_value_block = generator.make_block();

    generator.switch_to_basic_block(throw_completion_continuation_block);
    generator.emit<Bytecode::Op::LoadImmediate>(Value(to_underlying(Completion::Type::Throw)));
    generator.emit<Bytecode::Op::StrictlyEquals>(received_completion_type_register);

    // If type is not equal to "throw" or "normal", assume it's "return".
    generator.emit<Bytecode::Op::JumpConditional>(
        Bytecode::Label { throw_value_block },
        Bytecode::Label { return_value_block });

    generator.switch_to_basic_block(throw_value_block);
    generator.emit<Bytecode::Op::Load>(received_completion_value_register);
    generator.perform_needed_unwinds<Bytecode::Op::Throw>();
    generator.emit<Bytecode::Op::Throw>();

    generator.switch_to_basic_block(return_value_block);
    generator.emit<Bytecode::Op::Load>(received_completion_value_register);
    generator.perform_needed_unwinds<Bytecode::Op::Yield>();
    generator.emit<Bytecode::Op::Yield>(nullptr);

    generator.switch_to_basic_block(normal_completion_continuation_block);
    generator.emit<Bytecode::Op::Load>(received_completion_value_register);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> IfStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    // test
    // jump if_true (true) true (false) false
    // true
    // jump always (true) end
    // false
    // jump always (true) end
    // end

    auto& true_block = generator.make_block();
    auto& false_block = generator.make_block();

    TRY(m_predicate->generate_bytecode(generator));
    generator.emit<Bytecode::Op::JumpConditional>().set_targets(
        Bytecode::Label { true_block },
        Bytecode::Label { false_block });

    Bytecode::Op::Jump* true_block_jump { nullptr };

    generator.switch_to_basic_block(true_block);
    generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
    TRY(m_consequent->generate_bytecode(generator));
    if (!generator.is_current_block_terminated())
        true_block_jump = &generator.emit<Bytecode::Op::Jump>();

    generator.switch_to_basic_block(false_block);
    auto& end_block = generator.make_block();

    generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
    if (m_alternate)
        TRY(m_alternate->generate_bytecode(generator));
    if (!generator.is_current_block_terminated())
        generator.emit<Bytecode::Op::Jump>().set_targets(Bytecode::Label { end_block }, {});

    if (true_block_jump)
        true_block_jump->set_targets(Bytecode::Label { end_block }, {});

    generator.switch_to_basic_block(end_block);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ContinueStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    // FIXME: Handle finally blocks in a graceful manner
    //        We need to execute the finally block, but tell it to resume
    //        execution at the designated block
    if (m_target_label.is_null()) {
        generator.generate_continue();
        return {};
    }

    generator.generate_continue(m_target_label);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> DebuggerStatement::generate_bytecode(Bytecode::Generator&) const
{
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ConditionalExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    // test
    // jump if_true (true) true (false) false
    // true
    // jump always (true) end
    // false
    // jump always (true) end
    // end

    auto& true_block = generator.make_block();
    auto& false_block = generator.make_block();
    auto& end_block = generator.make_block();

    TRY(m_test->generate_bytecode(generator));
    generator.emit<Bytecode::Op::JumpConditional>().set_targets(
        Bytecode::Label { true_block },
        Bytecode::Label { false_block });

    generator.switch_to_basic_block(true_block);
    TRY(m_consequent->generate_bytecode(generator));
    generator.emit<Bytecode::Op::Jump>().set_targets(
        Bytecode::Label { end_block },
        {});

    generator.switch_to_basic_block(false_block);
    TRY(m_alternate->generate_bytecode(generator));
    generator.emit<Bytecode::Op::Jump>().set_targets(
        Bytecode::Label { end_block },
        {});

    generator.switch_to_basic_block(end_block);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> SequenceExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    for (auto& expression : m_expressions)
        TRY(expression->generate_bytecode(generator));

    return {};
}

Bytecode::CodeGenerationErrorOr<void> TemplateLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    auto string_reg = generator.allocate_register();

    for (size_t i = 0; i < m_expressions.size(); i++) {
        TRY(m_expressions[i]->generate_bytecode(generator));
        if (i == 0) {
            generator.emit<Bytecode::Op::Store>(string_reg);
        } else {
            generator.emit<Bytecode::Op::ConcatString>(string_reg);
        }
    }

    generator.emit<Bytecode::Op::Load>(string_reg);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> TaggedTemplateLiteral::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(m_tag->generate_bytecode(generator));
    auto tag_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(tag_reg);

    // FIXME: We only need to record the first and last register,
    //        due to packing everything in an array, same goes for argument_regs
    Vector<Bytecode::Register> string_regs;
    auto& expressions = m_template_literal->expressions();
    for (size_t i = 0; i < expressions.size(); ++i) {
        if (i % 2 != 0)
            continue;
        string_regs.append(generator.allocate_register());
    }

    size_t reg_index = 0;
    for (size_t i = 0; i < expressions.size(); ++i) {
        if (i % 2 != 0)
            continue;

        TRY(expressions[i]->generate_bytecode(generator));
        auto string_reg = string_regs[reg_index++];
        generator.emit<Bytecode::Op::Store>(string_reg);
    }

    if (string_regs.is_empty()) {
        generator.emit<Bytecode::Op::NewArray>();
    } else {
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2u, AK::Array { string_regs.first(), string_regs.last() });
    }
    auto strings_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(strings_reg);

    Vector<Bytecode::Register> argument_regs;
    argument_regs.append(strings_reg);
    for (size_t i = 1; i < expressions.size(); i += 2)
        argument_regs.append(generator.allocate_register());

    for (size_t i = 1; i < expressions.size(); i += 2) {
        auto string_reg = argument_regs[1 + i / 2];
        TRY(expressions[i]->generate_bytecode(generator));
        generator.emit<Bytecode::Op::Store>(string_reg);
    }

    Vector<Bytecode::Register> raw_string_regs;
    for ([[maybe_unused]] auto& raw_string : m_template_literal->raw_strings())
        string_regs.append(generator.allocate_register());

    reg_index = 0;
    for (auto& raw_string : m_template_literal->raw_strings()) {
        TRY(raw_string->generate_bytecode(generator));
        auto raw_string_reg = string_regs[reg_index++];
        generator.emit<Bytecode::Op::Store>(raw_string_reg);
        raw_string_regs.append(raw_string_reg);
    }

    if (raw_string_regs.is_empty()) {
        generator.emit<Bytecode::Op::NewArray>();
    } else {
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2u, AK::Array { raw_string_regs.first(), raw_string_regs.last() });
    }
    auto raw_strings_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(raw_strings_reg);

    generator.emit<Bytecode::Op::PutById>(strings_reg, generator.intern_identifier("raw"));

    generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
    auto this_reg = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(this_reg);

    if (!argument_regs.is_empty())
        generator.emit_with_extra_register_slots<Bytecode::Op::NewArray>(2, AK::Array { argument_regs.first(), argument_regs.last() });
    else
        generator.emit<Bytecode::Op::NewArray>();

    generator.emit<Bytecode::Op::Call>(Bytecode::Op::Call::CallType::Call, tag_reg, this_reg);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> UpdateExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(generator.emit_load_from_reference(*m_argument));

    Optional<Bytecode::Register> previous_value_for_postfix_reg;
    if (!m_prefixed) {
        previous_value_for_postfix_reg = generator.allocate_register();
        generator.emit<Bytecode::Op::ToNumeric>();
        generator.emit<Bytecode::Op::Store>(*previous_value_for_postfix_reg);
    }

    if (m_op == UpdateOp::Increment)
        generator.emit<Bytecode::Op::Increment>();
    else
        generator.emit<Bytecode::Op::Decrement>();

    TRY(generator.emit_store_to_reference(*m_argument));

    if (!m_prefixed)
        generator.emit<Bytecode::Op::Load>(*previous_value_for_postfix_reg);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ThrowStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(m_argument->generate_bytecode(generator));
    generator.perform_needed_unwinds<Bytecode::Op::Throw>();
    generator.emit<Bytecode::Op::Throw>();
    return {};
}

Bytecode::CodeGenerationErrorOr<void> BreakStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    // FIXME: Handle finally blocks in a graceful manner
    //        We need to execute the finally block, but tell it to resume
    //        execution at the designated block
    if (m_target_label.is_null()) {
        generator.generate_break();
        return {};
    }

    generator.generate_break(m_target_label);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> TryStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    auto& saved_block = generator.current_block();

    Optional<Bytecode::Label> handler_target;
    Optional<Bytecode::Label> finalizer_target;

    Bytecode::BasicBlock* next_block { nullptr };

    if (m_finalizer) {
        // FIXME: See notes in Op.h->ScheduleJump
        auto& finalizer_block = generator.make_block();
        generator.switch_to_basic_block(finalizer_block);
        generator.emit<Bytecode::Op::LeaveUnwindContext>();
        TRY(m_finalizer->generate_bytecode(generator));
        if (!generator.is_current_block_terminated()) {
            next_block = &generator.make_block();
            auto next_target = Bytecode::Label { *next_block };
            generator.emit<Bytecode::Op::ContinuePendingUnwind>(next_target);
        }
        finalizer_target = Bytecode::Label { finalizer_block };
    }

    if (m_finalizer)
        generator.start_boundary(Bytecode::Generator::BlockBoundaryType::ReturnToFinally);
    if (m_handler) {
        auto& handler_block = generator.make_block();
        generator.switch_to_basic_block(handler_block);

        if (!m_finalizer)
            generator.emit<Bytecode::Op::LeaveUnwindContext>();

        generator.begin_variable_scope();
        TRY(m_handler->parameter().visit(
            [&](DeprecatedFlyString const& parameter) -> Bytecode::CodeGenerationErrorOr<void> {
                if (!parameter.is_empty()) {
                    auto parameter_identifier = generator.intern_identifier(parameter);
                    generator.emit<Bytecode::Op::CreateVariable>(parameter_identifier, Bytecode::Op::EnvironmentMode::Lexical, false);
                    generator.emit<Bytecode::Op::SetVariable>(parameter_identifier, Bytecode::Op::SetVariable::InitializationMode::Initialize);
                }
                return {};
            },
            [&](NonnullRefPtr<BindingPattern const> const&) -> Bytecode::CodeGenerationErrorOr<void> {
                // FIXME: Implement this path when the above DeclarativeEnvironment issue is dealt with.
                return Bytecode::CodeGenerationError {
                    this,
                    "Unimplemented catch argument: BindingPattern"sv,
                };
            }));

        TRY(m_handler->body().generate_bytecode(generator));
        handler_target = Bytecode::Label { handler_block };
        generator.end_variable_scope();

        if (!generator.is_current_block_terminated()) {
            if (m_finalizer) {
                generator.emit<Bytecode::Op::Jump>(finalizer_target);
            } else {
                VERIFY(!next_block);
                next_block = &generator.make_block();
                auto next_target = Bytecode::Label { *next_block };
                generator.emit<Bytecode::Op::Jump>(next_target);
            }
        }
    }
    if (m_finalizer)
        generator.end_boundary(Bytecode::Generator::BlockBoundaryType::ReturnToFinally);

    auto& target_block = generator.make_block();
    generator.switch_to_basic_block(saved_block);
    generator.emit<Bytecode::Op::EnterUnwindContext>(Bytecode::Label { target_block }, handler_target, finalizer_target);
    generator.start_boundary(Bytecode::Generator::BlockBoundaryType::Unwind);
    if (m_finalizer)
        generator.start_boundary(Bytecode::Generator::BlockBoundaryType::ReturnToFinally);

    generator.switch_to_basic_block(target_block);
    TRY(m_block->generate_bytecode(generator));
    if (!generator.is_current_block_terminated()) {
        if (m_finalizer) {
            generator.emit<Bytecode::Op::Jump>(finalizer_target);
        } else {
            if (!next_block)
                next_block = &generator.make_block();
            generator.emit<Bytecode::Op::LeaveUnwindContext>();
            generator.emit<Bytecode::Op::Jump>(Bytecode::Label { *next_block });
        }
    }

    if (m_finalizer)
        generator.end_boundary(Bytecode::Generator::BlockBoundaryType::ReturnToFinally);
    generator.end_boundary(Bytecode::Generator::BlockBoundaryType::Unwind);

    generator.switch_to_basic_block(next_block ? *next_block : saved_block);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> SwitchStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    return generate_labelled_evaluation(generator, {});
}

Bytecode::CodeGenerationErrorOr<void> SwitchStatement::generate_labelled_evaluation(Bytecode::Generator& generator, Vector<DeprecatedFlyString> const& label_set) const
{
    auto discriminant_reg = generator.allocate_register();
    TRY(m_discriminant->generate_bytecode(generator));
    generator.emit<Bytecode::Op::Store>(discriminant_reg);
    Vector<Bytecode::BasicBlock&> case_blocks;
    Bytecode::BasicBlock* default_block { nullptr };
    Bytecode::BasicBlock* next_test_block = &generator.make_block();

    auto has_lexical_block = has_lexical_declarations();
    // Note: This call ends up calling begin_variable_scope() if has_lexical_block is true, so we need to clean up after it at the end.
    TRY(ScopeNode::generate_bytecode(generator));

    generator.emit<Bytecode::Op::Jump>().set_targets(Bytecode::Label { *next_test_block }, {});

    for (auto& switch_case : m_cases) {
        auto& case_block = generator.make_block();
        if (switch_case->test()) {
            generator.switch_to_basic_block(*next_test_block);
            TRY(switch_case->test()->generate_bytecode(generator));
            generator.emit<Bytecode::Op::StrictlyEquals>(discriminant_reg);
            next_test_block = &generator.make_block();
            generator.emit<Bytecode::Op::JumpConditional>().set_targets(Bytecode::Label { case_block }, Bytecode::Label { *next_test_block });
        } else {
            default_block = &case_block;
        }
        case_blocks.append(case_block);
    }
    generator.switch_to_basic_block(*next_test_block);
    auto& end_block = generator.make_block();

    if (default_block != nullptr) {
        generator.emit<Bytecode::Op::Jump>().set_targets(Bytecode::Label { *default_block }, {});
    } else {
        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
        generator.emit<Bytecode::Op::Jump>().set_targets(Bytecode::Label { end_block }, {});
    }
    auto current_block = case_blocks.begin();
    generator.begin_breakable_scope(Bytecode::Label { end_block }, label_set);
    for (auto& switch_case : m_cases) {
        generator.switch_to_basic_block(*current_block);

        generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
        for (auto& statement : switch_case->children()) {
            TRY(statement->generate_bytecode(generator));
            if (generator.is_current_block_terminated())
                break;
        }
        if (!generator.is_current_block_terminated()) {
            auto next_block = current_block;
            next_block++;
            if (next_block.is_end()) {
                generator.emit<Bytecode::Op::Jump>().set_targets(Bytecode::Label { end_block }, {});
            } else {
                generator.emit<Bytecode::Op::Jump>().set_targets(Bytecode::Label { *next_block }, {});
            }
        }
        current_block++;
    }
    generator.end_breakable_scope();
    if (has_lexical_block)
        generator.end_variable_scope();

    generator.switch_to_basic_block(end_block);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ClassDeclaration::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(m_class_expression->generate_bytecode(generator));
    generator.emit<Bytecode::Op::SetVariable>(generator.intern_identifier(m_class_expression.ptr()->name()), Bytecode::Op::SetVariable::InitializationMode::Initialize);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ClassExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::NewClass>(*this);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> SpreadExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    // NOTE: All users of this should handle the behaviour of this on their own,
    //       assuming it returns an Array-like object
    return m_target->generate_bytecode(generator);
}

Bytecode::CodeGenerationErrorOr<void> ThisExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    generator.emit<Bytecode::Op::ResolveThisBinding>();
    return {};
}

Bytecode::CodeGenerationErrorOr<void> AwaitExpression::generate_bytecode(Bytecode::Generator& generator) const
{
    VERIFY(generator.is_in_async_function());

    // Transform `await expr` to `yield expr`, see AsyncFunctionDriverWrapper
    // For that we just need to copy most of the code from YieldExpression
    auto received_completion_register = generator.allocate_register();
    auto received_completion_type_register = generator.allocate_register();
    auto received_completion_value_register = generator.allocate_register();

    auto type_identifier = generator.intern_identifier("type");
    auto value_identifier = generator.intern_identifier("value");

    TRY(m_argument->generate_bytecode(generator));

    auto& continuation_block = generator.make_block();
    generator.emit<Bytecode::Op::Yield>(Bytecode::Label { continuation_block });
    generator.switch_to_basic_block(continuation_block);

    // The accumulator is set to an object, for example: { "type": 1 (normal), value: 1337 }
    generator.emit<Bytecode::Op::Store>(received_completion_register);

    generator.emit<Bytecode::Op::GetById>(type_identifier);
    generator.emit<Bytecode::Op::Store>(received_completion_type_register);

    generator.emit<Bytecode::Op::Load>(received_completion_register);
    generator.emit<Bytecode::Op::GetById>(value_identifier);
    generator.emit<Bytecode::Op::Store>(received_completion_value_register);

    auto& normal_completion_continuation_block = generator.make_block();
    auto& throw_value_block = generator.make_block();

    generator.emit<Bytecode::Op::LoadImmediate>(Value(to_underlying(Completion::Type::Normal)));
    generator.emit<Bytecode::Op::StrictlyEquals>(received_completion_type_register);
    generator.emit<Bytecode::Op::JumpConditional>(
        Bytecode::Label { normal_completion_continuation_block },
        Bytecode::Label { throw_value_block });

    // Simplification: The only abrupt completion we receive from AsyncFunctionDriverWrapper is Type::Throw
    //                 So we do not need to account for the Type::Return path
    generator.switch_to_basic_block(throw_value_block);
    generator.emit<Bytecode::Op::Load>(received_completion_value_register);
    generator.perform_needed_unwinds<Bytecode::Op::Throw>();
    generator.emit<Bytecode::Op::Throw>();

    generator.switch_to_basic_block(normal_completion_continuation_block);
    generator.emit<Bytecode::Op::Load>(received_completion_value_register);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> WithStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(m_object->generate_bytecode(generator));
    generator.emit<Bytecode::Op::EnterObjectEnvironment>();

    // EnterObjectEnvironment sets the running execution context's lexical_environment to a new Object Environment.
    generator.start_boundary(Bytecode::Generator::BlockBoundaryType::LeaveLexicalEnvironment);
    TRY(m_body->generate_bytecode(generator));
    generator.end_boundary(Bytecode::Generator::BlockBoundaryType::LeaveLexicalEnvironment);

    if (!generator.is_current_block_terminated())
        generator.emit<Bytecode::Op::LeaveLexicalEnvironment>();

    return {};
}

enum class LHSKind {
    Assignment,
    VarBinding,
    LexicalBinding,
};

enum class IterationKind {
    Enumerate,
    Iterate,
    AsyncIterate,
};

// 14.7.5.6 ForIn/OfHeadEvaluation ( uninitializedBoundNames, expr, iterationKind ), https://tc39.es/ecma262/#sec-runtime-semantics-forinofheadevaluation
struct ForInOfHeadEvaluationResult {
    bool is_destructuring { false };
    LHSKind lhs_kind { LHSKind::Assignment };
};
static Bytecode::CodeGenerationErrorOr<ForInOfHeadEvaluationResult> for_in_of_head_evaluation(Bytecode::Generator& generator, IterationKind iteration_kind, Variant<NonnullRefPtr<ASTNode const>, NonnullRefPtr<BindingPattern const>> const& lhs, NonnullRefPtr<ASTNode const> const& rhs)
{
    ForInOfHeadEvaluationResult result {};

    bool entered_lexical_scope = false;
    if (auto* ast_ptr = lhs.get_pointer<NonnullRefPtr<ASTNode const>>(); ast_ptr && is<VariableDeclaration>(**ast_ptr)) {
        // Runtime Semantics: ForInOfLoopEvaluation, for any of:
        //  ForInOfStatement : for ( var ForBinding in Expression ) Statement
        //  ForInOfStatement : for ( ForDeclaration in Expression ) Statement
        //  ForInOfStatement : for ( var ForBinding of AssignmentExpression ) Statement
        //  ForInOfStatement : for ( ForDeclaration of AssignmentExpression ) Statement

        auto& variable_declaration = static_cast<VariableDeclaration const&>(**ast_ptr);
        result.is_destructuring = variable_declaration.declarations().first()->target().has<NonnullRefPtr<BindingPattern const>>();
        result.lhs_kind = variable_declaration.is_lexical_declaration() ? LHSKind::LexicalBinding : LHSKind::VarBinding;

        // 1. Let oldEnv be the running execution context's LexicalEnvironment.

        // NOTE: 'uninitializedBoundNames' refers to the lexical bindings (i.e. Const/Let) present in the second and last form.
        // 2. If uninitializedBoundNames is not an empty List, then

        if (variable_declaration.declaration_kind() != DeclarationKind::Var) {
            entered_lexical_scope = true;
            // a. Assert: uninitializedBoundNames has no duplicate entries.
            // b. Let newEnv be NewDeclarativeEnvironment(oldEnv).
            generator.begin_variable_scope();
            // c. For each String name of uninitializedBoundNames, do
            // NOTE: Nothing in the callback throws an exception.
            MUST(variable_declaration.for_each_bound_name([&](auto const& name) {
                // i. Perform ! newEnv.CreateMutableBinding(name, false).
                auto identifier = generator.intern_identifier(name);
                generator.emit<Bytecode::Op::CreateVariable>(identifier, Bytecode::Op::EnvironmentMode::Lexical, false);
            }));
            // d. Set the running execution context's LexicalEnvironment to newEnv.
            // NOTE: Done by CreateLexicalEnvironment.
        }
    } else {
        // Runtime Semantics: ForInOfLoopEvaluation, for any of:
        //  ForInOfStatement : for ( LeftHandSideExpression in Expression ) Statement
        //  ForInOfStatement : for ( LeftHandSideExpression of AssignmentExpression ) Statement
        result.lhs_kind = LHSKind::Assignment;
    }

    // 3. Let exprRef be the result of evaluating expr.
    TRY(rhs->generate_bytecode(generator));

    // 4. Set the running execution context's LexicalEnvironment to oldEnv.
    if (entered_lexical_scope)
        generator.end_variable_scope();

    // 5. Let exprValue be ? GetValue(exprRef).
    // NOTE: No need to store this anywhere.

    // 6. If iterationKind is enumerate, then
    if (iteration_kind == IterationKind::Enumerate) {
        // a. If exprValue is undefined or null, then
        auto& nullish_block = generator.make_block();
        auto& continuation_block = generator.make_block();
        auto& jump = generator.emit<Bytecode::Op::JumpNullish>();
        jump.set_targets(Bytecode::Label { nullish_block }, Bytecode::Label { continuation_block });

        // i. Return Completion Record { [[Type]]: break, [[Value]]: empty, [[Target]]: empty }.
        generator.switch_to_basic_block(nullish_block);
        generator.generate_break();

        generator.switch_to_basic_block(continuation_block);
        // b. Let obj be ! ToObject(exprValue).
        // NOTE: GetObjectPropertyIterator does this.
        // c. Let iterator be EnumerateObjectProperties(obj).
        // d. Let nextMethod be ! GetV(iterator, "next").
        // e. Return the Iterator Record { [[Iterator]]: iterator, [[NextMethod]]: nextMethod, [[Done]]: false }.
        generator.emit<Bytecode::Op::GetObjectPropertyIterator>();
    }
    // 7. Else,
    else {
        // a. Assert: iterationKind is iterate or async-iterate.
        // b. If iterationKind is async-iterate, let iteratorHint be async.
        if (iteration_kind == IterationKind::AsyncIterate) {
            return Bytecode::CodeGenerationError {
                rhs.ptr(),
                "Unimplemented iteration mode: AsyncIterate"sv,
            };
        }
        // c. Else, let iteratorHint be sync.

        // d. Return ? GetIterator(exprValue, iteratorHint).
        generator.emit<Bytecode::Op::GetIterator>();
    }

    return result;
}

// 14.7.5.7 ForIn/OfBodyEvaluation ( lhs, stmt, iteratorRecord, iterationKind, lhsKind, labelSet [ , iteratorKind ] ), https://tc39.es/ecma262/#sec-runtime-semantics-forin-div-ofbodyevaluation-lhs-stmt-iterator-lhskind-labelset
static Bytecode::CodeGenerationErrorOr<void> for_in_of_body_evaluation(Bytecode::Generator& generator, ASTNode const& node, Variant<NonnullRefPtr<ASTNode const>, NonnullRefPtr<BindingPattern const>> const& lhs, ASTNode const& body, ForInOfHeadEvaluationResult const& head_result, Vector<DeprecatedFlyString> const& label_set, Bytecode::BasicBlock& loop_end, Bytecode::BasicBlock& loop_update)
{
    auto iterator_register = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(iterator_register);

    // FIXME: Implement this
    //        1. If iteratorKind is not present, set iteratorKind to sync.

    // 2. Let oldEnv be the running execution context's LexicalEnvironment.
    bool has_lexical_binding = false;

    // 3. Let V be undefined.
    // NOTE: We don't need 'V' as the resulting value will naturally flow through via the accumulator register.

    // 4. Let destructuring be IsDestructuring of lhs.
    auto destructuring = head_result.is_destructuring;

    // 5. If destructuring is true and if lhsKind is assignment, then
    if (destructuring && head_result.lhs_kind == LHSKind::Assignment) {
        // a. Assert: lhs is a LeftHandSideExpression.
        // b. Let assignmentPattern be the AssignmentPattern that is covered by lhs.
        // FIXME: Implement this.
        return Bytecode::CodeGenerationError {
            &node,
            "Unimplemented: assignment destructuring in for/of"sv,
        };
    }
    // 6. Repeat,
    generator.emit<Bytecode::Op::Jump>(Bytecode::Label { loop_update });
    generator.switch_to_basic_block(loop_update);
    generator.begin_continuable_scope(Bytecode::Label { loop_update }, label_set);

    // a. Let nextResult be ? Call(iteratorRecord.[[NextMethod]], iteratorRecord.[[Iterator]]).
    generator.emit<Bytecode::Op::Load>(iterator_register);
    generator.emit<Bytecode::Op::IteratorNext>();

    // FIXME: Implement this:
    //        b. If iteratorKind is async, set nextResult to ? Await(nextResult).

    // c. If Type(nextResult) is not Object, throw a TypeError exception.
    // NOTE: IteratorComplete already does this.

    // d. Let done be ? IteratorComplete(nextResult).
    auto iterator_result_register = generator.allocate_register();
    generator.emit<Bytecode::Op::Store>(iterator_result_register);

    generator.emit<Bytecode::Op::IteratorResultDone>();
    // e. If done is true, return V.
    auto& loop_continue = generator.make_block();
    generator.emit<Bytecode::Op::JumpConditional>().set_targets(Bytecode::Label { loop_end }, Bytecode::Label { loop_continue });
    generator.switch_to_basic_block(loop_continue);

    // f. Let nextValue be ? IteratorValue(nextResult).
    generator.emit<Bytecode::Op::Load>(iterator_result_register);
    generator.emit<Bytecode::Op::IteratorResultValue>();

    // g. If lhsKind is either assignment or varBinding, then
    if (head_result.lhs_kind != LHSKind::LexicalBinding) {
        // i. If destructuring is false, then
        if (!destructuring) {
            // 1. Let lhsRef be the result of evaluating lhs. (It may be evaluated repeatedly.)
            // NOTE: We're skipping all the completion stuff that the spec does, as the unwinding mechanism will take case of doing that.
            if (head_result.lhs_kind == LHSKind::VarBinding) {
                auto& declaration = static_cast<VariableDeclaration const&>(*lhs.get<NonnullRefPtr<ASTNode const>>());
                VERIFY(declaration.declarations().size() == 1);
                TRY(assign_accumulator_to_variable_declarator(generator, declaration.declarations().first(), declaration));
            } else {
                if (auto ptr = lhs.get_pointer<NonnullRefPtr<ASTNode const>>()) {
                    TRY(generator.emit_store_to_reference(**ptr));
                } else {
                    auto& binding_pattern = lhs.get<NonnullRefPtr<BindingPattern const>>();
                    auto value_register = generator.allocate_register();
                    generator.emit<Bytecode::Op::Store>(value_register);
                    TRY(generate_binding_pattern_bytecode(generator, *binding_pattern, Bytecode::Op::SetVariable::InitializationMode::Set, value_register));
                }
            }
        }
    }
    // h. Else,
    else {
        // i. Assert: lhsKind is lexicalBinding.
        // ii. Assert: lhs is a ForDeclaration.
        // iii. Let iterationEnv be NewDeclarativeEnvironment(oldEnv).
        // iv. Perform ForDeclarationBindingInstantiation of lhs with argument iterationEnv.
        // v. Set the running execution context's LexicalEnvironment to iterationEnv.
        generator.begin_variable_scope();
        has_lexical_binding = true;

        // 14.7.5.4 Runtime Semantics: ForDeclarationBindingInstantiation, https://tc39.es/ecma262/#sec-runtime-semantics-fordeclarationbindinginstantiation
        // 1. Assert: environment is a declarative Environment Record.
        // NOTE: We just made it.
        auto& variable_declaration = static_cast<VariableDeclaration const&>(*lhs.get<NonnullRefPtr<ASTNode const>>());
        // 2. For each element name of the BoundNames of ForBinding, do
        // NOTE: Nothing in the callback throws an exception.
        MUST(variable_declaration.for_each_bound_name([&](auto const& name) {
            auto identifier = generator.intern_identifier(name);
            // a. If IsConstantDeclaration of LetOrConst is true, then
            if (variable_declaration.is_constant_declaration()) {
                // i. Perform ! environment.CreateImmutableBinding(name, true).
                generator.emit<Bytecode::Op::CreateVariable>(identifier, Bytecode::Op::EnvironmentMode::Lexical, true);
            }
            // b. Else,
            else {
                // i. Perform ! environment.CreateMutableBinding(name, false).
                generator.emit<Bytecode::Op::CreateVariable>(identifier, Bytecode::Op::EnvironmentMode::Lexical, false);
            }
        }));
        // 3. Return unused.
        // NOTE: No need to do that as we've inlined this.

        // vi. If destructuring is false, then
        if (!destructuring) {
            // 1. Assert: lhs binds a single name.
            // 2. Let lhsName be the sole element of BoundNames of lhs.
            auto lhs_name = variable_declaration.declarations().first()->target().get<NonnullRefPtr<Identifier const>>()->string();
            // 3. Let lhsRef be ! ResolveBinding(lhsName).
            // NOTE: We're skipping all the completion stuff that the spec does, as the unwinding mechanism will take case of doing that.
            auto identifier = generator.intern_identifier(lhs_name);
            generator.emit<Bytecode::Op::SetVariable>(identifier, Bytecode::Op::SetVariable::InitializationMode::Initialize, Bytecode::Op::EnvironmentMode::Lexical);
        }
    }
    // i. If destructuring is false, then
    if (!destructuring) {
        // i. If lhsRef is an abrupt completion, then
        //     1. Let status be lhsRef.
        // ii. Else if lhsKind is lexicalBinding, then
        //     1. Let status be Completion(InitializeReferencedBinding(lhsRef, nextValue)).
        // iii. Else,
        //     1. Let status be Completion(PutValue(lhsRef, nextValue)).
        // NOTE: This is performed above.
    }
    //    j. Else,
    else {
        // FIXME: i. If lhsKind is assignment, then
        //           1. Let status be Completion(DestructuringAssignmentEvaluation of assignmentPattern with argument nextValue).

        //  ii. Else if lhsKind is varBinding, then
        //      1. Assert: lhs is a ForBinding.
        //      2. Let status be Completion(BindingInitialization of lhs with arguments nextValue and undefined).
        //  iii. Else,
        //      1. Assert: lhsKind is lexicalBinding.
        //      2. Assert: lhs is a ForDeclaration.
        //      3. Let status be Completion(ForDeclarationBindingInitialization of lhs with arguments nextValue and iterationEnv).
        if (head_result.lhs_kind == LHSKind::VarBinding || head_result.lhs_kind == LHSKind::LexicalBinding) {
            auto& declaration = static_cast<VariableDeclaration const&>(*lhs.get<NonnullRefPtr<ASTNode const>>());
            VERIFY(declaration.declarations().size() == 1);
            auto& binding_pattern = declaration.declarations().first()->target().get<NonnullRefPtr<BindingPattern const>>();

            auto value_register = generator.allocate_register();
            generator.emit<Bytecode::Op::Store>(value_register);
            TRY(generate_binding_pattern_bytecode(generator, *binding_pattern, head_result.lhs_kind == LHSKind::VarBinding ? Bytecode::Op::SetVariable::InitializationMode::Set : Bytecode::Op::SetVariable::InitializationMode::Initialize, value_register));
        } else {
            return Bytecode::CodeGenerationError {
                &node,
                "Unimplemented: assignment destructuring in for/of"sv,
            };
        }
    }

    // FIXME: Implement iteration closure.
    // k. If status is an abrupt completion, then
    //     i. Set the running execution context's LexicalEnvironment to oldEnv.
    //     ii. If iteratorKind is async, return ? AsyncIteratorClose(iteratorRecord, status).
    //     iii. If iterationKind is enumerate, then
    //         1. Return ? status.
    //     iv. Else,
    //         1. Assert: iterationKind is iterate.
    //         2. Return ? IteratorClose(iteratorRecord, status).

    // l. Let result be the result of evaluating stmt.
    TRY(body.generate_bytecode(generator));

    // m. Set the running execution context's LexicalEnvironment to oldEnv.
    if (has_lexical_binding)
        generator.end_variable_scope();
    generator.end_continuable_scope();
    generator.end_breakable_scope();

    // NOTE: If we're here, then the loop definitely continues.
    // n. If LoopContinues(result, labelSet) is false, then
    //     i. If iterationKind is enumerate, then
    //         1. Return ? UpdateEmpty(result, V).
    //     ii. Else,
    //         1. Assert: iterationKind is iterate.
    //         2. Set status to Completion(UpdateEmpty(result, V)).
    //         3. If iteratorKind is async, return ? AsyncIteratorClose(iteratorRecord, status).
    //         4. Return ? IteratorClose(iteratorRecord, status).
    // o. If result.[[Value]] is not empty, set V to result.[[Value]].

    // The body can contain an unconditional block terminator (e.g. return, throw), so we have to check for that before generating the Jump.
    if (!generator.is_current_block_terminated())
        generator.emit<Bytecode::Op::Jump>().set_targets(Bytecode::Label { loop_update }, {});

    generator.switch_to_basic_block(loop_end);
    return {};
}

Bytecode::CodeGenerationErrorOr<void> ForInStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    return generate_labelled_evaluation(generator, {});
}

// 14.7.5.5 Runtime Semantics: ForInOfLoopEvaluation, https://tc39.es/ecma262/#sec-runtime-semantics-forinofloopevaluation
Bytecode::CodeGenerationErrorOr<void> ForInStatement::generate_labelled_evaluation(Bytecode::Generator& generator, Vector<DeprecatedFlyString> const& label_set) const
{
    auto& loop_end = generator.make_block();
    auto& loop_update = generator.make_block();
    generator.begin_breakable_scope(Bytecode::Label { loop_end }, label_set);

    auto head_result = TRY(for_in_of_head_evaluation(generator, IterationKind::Enumerate, m_lhs, m_rhs));

    // Now perform the rest of ForInOfLoopEvaluation, given that the accumulator holds the iterator we're supposed to iterate over.
    return for_in_of_body_evaluation(generator, *this, m_lhs, body(), head_result, label_set, loop_end, loop_update);
}

Bytecode::CodeGenerationErrorOr<void> ForOfStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    return generate_labelled_evaluation(generator, {});
}

Bytecode::CodeGenerationErrorOr<void> ForOfStatement::generate_labelled_evaluation(Bytecode::Generator& generator, Vector<DeprecatedFlyString> const& label_set) const
{
    auto& loop_end = generator.make_block();
    auto& loop_update = generator.make_block();
    generator.begin_breakable_scope(Bytecode::Label { loop_end }, label_set);

    auto head_result = TRY(for_in_of_head_evaluation(generator, IterationKind::Iterate, m_lhs, m_rhs));

    // Now perform the rest of ForInOfLoopEvaluation, given that the accumulator holds the iterator we're supposed to iterate over.
    return for_in_of_body_evaluation(generator, *this, m_lhs, body(), head_result, label_set, loop_end, loop_update);
}

// 13.3.12.1 Runtime Semantics: Evaluation, https://tc39.es/ecma262/#sec-meta-properties-runtime-semantics-evaluation
Bytecode::CodeGenerationErrorOr<void> MetaProperty::generate_bytecode(Bytecode::Generator& generator) const
{
    // NewTarget : new . target
    if (m_type == MetaProperty::Type::NewTarget) {
        // 1. Return GetNewTarget().
        generator.emit<Bytecode::Op::GetNewTarget>();
        return {};
    }

    // ImportMeta : import . meta
    if (m_type == MetaProperty::Type::ImportMeta) {
        return Bytecode::CodeGenerationError {
            this,
            "Unimplemented meta property: import.meta"sv,
        };
    }

    VERIFY_NOT_REACHED();
}

Bytecode::CodeGenerationErrorOr<void> ClassFieldInitializerStatement::generate_bytecode(Bytecode::Generator& generator) const
{
    TRY(m_expression->generate_bytecode(generator));
    generator.perform_needed_unwinds<Bytecode::Op::Return>();
    generator.emit<Bytecode::Op::Return>();
    return {};
}

}
