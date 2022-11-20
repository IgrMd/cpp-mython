#include "statement.h"

#include <cassert>
#include <iostream>
#include <optional>
#include <sstream>

using namespace std;

namespace ast {

using runtime::Closure;
using runtime::Context;
using runtime::ObjectHolder;

namespace {
const string ADD_METHOD = "__add__"s;
const string INIT_METHOD = "__init__"s;
}  // namespace


// ----- Assignment -----
Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv)
	: var_(std::move(var))
	, rv_(std::move(rv)) {
}

ObjectHolder Assignment::Execute(Closure& closure, Context& context) {
	closure[var_] = rv_->Execute(closure, context);
	return ObjectHolder::Share(*closure.at(var_).Get());
}

// ----- VariableValue -----
VariableValue::VariableValue(const std::string& var_name)
	: ids_({ var_name }) {
}

VariableValue::VariableValue(std::vector<std::string> dotted_ids)
	: ids_(std::move(dotted_ids)) {
}

ObjectHolder VariableValue::Execute(Closure& closure, [[maybe_unused]] Context& context) {
	Closure* fields = &closure;
	for (size_t i = 0; i + 1 < ids_.size(); ++i) {
		if (!fields->count(ids_[i])) {
			throw std::runtime_error("Identifier \'"s + ids_[i] + "\' is undefined"s);
		}
		fields = &fields->at(ids_[i]).TryAs<runtime::ClassInstance>()->Fields();
	}
	if (!fields->count(ids_.back())) {
		throw std::runtime_error("Identifier \'"s + ids_.back() + "\' is undefined"s);
	}
	return fields->at(ids_.back());
}

// ----- Print -----
Print::Print(vector<unique_ptr<Statement>> args)
	:args_(std::move(args)) {
}

Print::Print(unique_ptr<Statement> argument) {
	args_.push_back(std::move(argument));
}

unique_ptr<Print> Print::Variable(const std::string& name) {
	return make_unique<Print>(make_unique<VariableValue>(name));
}

ObjectHolder Print::Execute(Closure& closure, Context& context) {
	std::ostringstream os;
	bool first = true;
	for (const auto& arg : args_) {
		if (!first) {
			os << ' ';
		}
		if (ObjectHolder oh = arg->Execute(closure, context)) {
			oh->Print(os, context);
		} else {
			os << "None"sv;
		}
		first = false;
	}
	os << '\n';
	context.GetOutputStream() << os.str();
	return ObjectHolder::Own(runtime::String{ os.str() });
}

static std::vector<ObjectHolder> GetActualArgs(const std::vector<std::unique_ptr<Statement>>& args,
	Closure& closure, Context& context) {
	std::vector<ObjectHolder> actual_args;
	for (const auto& arg : args) {
		actual_args.push_back(std::move(arg->Execute(closure, context)));
	}
	return actual_args;
}

MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
					   std::vector<std::unique_ptr<Statement>> args)
	: object_(std::move(object))
	, method_(std::move(method))
	, args_(std::move(args)) {
}

ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
	runtime::ClassInstance* obj = object_->Execute(closure, context).TryAs<runtime::ClassInstance>();
	if (obj && obj->HasMethod(method_, args_.size())) {
		std::vector<ObjectHolder> actual_args = GetActualArgs(args_, closure, context);
		return obj->Call(method_, actual_args, context);
	}
	return {};
}

ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
	std::ostringstream os;
	if (ObjectHolder oh = argument_->Execute(closure, context)) {
		oh->Print(os, context);
	} else {
		os << "None"sv;
	}
	return ObjectHolder::Own(runtime::String{ os.str() });
}

template<typename T>
static std::optional<ObjectHolder> AddSame(ObjectHolder& lhs, ObjectHolder& rhs) {
	if (lhs.TryAs<T>() && rhs.TryAs<T>()) {
		return ObjectHolder::Own<T>(lhs.TryAs<T>()->GetValue() + rhs.TryAs<T>()->GetValue());
	}
	return std::nullopt;
}

// ----- Binary Operations -----
ObjectHolder Add::Execute(Closure& closure, Context& context) {
	ObjectHolder lhs_oh = lhs_->Execute(closure, context);
	ObjectHolder rhs_oh = rhs_->Execute(closure, context);
	if (!lhs_oh || !rhs_oh) {
		throw std::runtime_error("Cannot add/concatenate objects"s);
	}
	if (auto numeric_result = AddSame<runtime::Number>(lhs_oh, rhs_oh)) {
		return std::move(*numeric_result);
	}
	if (auto string_result = AddSame<runtime::String>(lhs_oh, rhs_oh)) {
		return std::move(*string_result);
	}
	if (runtime::ClassInstance* lhs_class = lhs_oh.TryAs<runtime::ClassInstance>()) {
		if (!lhs_class->HasMethod(ADD_METHOD, 1)) {
			throw std::runtime_error("Cannot add/concatenate objects"s);
		}
		return lhs_class->Call(ADD_METHOD, { rhs_oh }, context);
	}
	throw std::runtime_error("Cannot add/concatenate objects"s);
	return {};
}

ObjectHolder Sub::Execute(Closure& closure, Context& context) {
	ObjectHolder lhs_oh = lhs_->Execute(closure, context);
	ObjectHolder rhs_oh = rhs_->Execute(closure, context);
	if (!lhs_oh || !rhs_oh) {
		throw std::runtime_error("Cannot sub objects"s);
	}
	if (lhs_oh.TryAs<runtime::Number>() && rhs_oh.TryAs<runtime::Number>()) {
		return ObjectHolder::Own<runtime::Number>(
				lhs_oh.TryAs<runtime::Number>()->GetValue() -
				rhs_oh.TryAs<runtime::Number>()->GetValue());
	}
	throw std::runtime_error("Cannot sub objects"s);
	return {};
}

ObjectHolder Mult::Execute(Closure& closure, Context& context) {
	ObjectHolder lhs_oh = lhs_->Execute(closure, context);
	ObjectHolder rhs_oh = rhs_->Execute(closure, context);
	if (!lhs_oh || !rhs_oh) {
		throw std::runtime_error("Cannot mult objects"s);
	}
	if (lhs_oh.TryAs<runtime::Number>() && rhs_oh.TryAs<runtime::Number>()) {
		return ObjectHolder::Own<runtime::Number>(
			lhs_oh.TryAs<runtime::Number>()->GetValue() *
			rhs_oh.TryAs<runtime::Number>()->GetValue());
	}
	throw std::runtime_error("Cannot mult objects"s);
	return {};
}

ObjectHolder Div::Execute(Closure& closure, Context& context) {
	ObjectHolder lhs_oh = lhs_->Execute(closure, context);
	ObjectHolder rhs_oh = rhs_->Execute(closure, context);
	if (!lhs_oh || !rhs_oh) {
		throw std::runtime_error("Cannot div objects"s);
	}
	if (lhs_oh.TryAs<runtime::Number>() && rhs_oh.TryAs<runtime::Number>()) {
		int rhs_value = rhs_oh.TryAs<runtime::Number>()->GetValue();
		if (rhs_value == 0) {
			throw std::runtime_error("Cannot div by zero"s);
		}
		return ObjectHolder::Own<runtime::Number>(
			lhs_oh.TryAs<runtime::Number>()->GetValue() /
			rhs_value);
	}
	throw std::runtime_error("Cannot div objects"s);
	return {};
}

ObjectHolder Compound::Execute(Closure& closure, Context& context) {
	for (const auto& stmt : stmts_) {
		stmt->Execute(closure, context);
	}
	return {};
}

ObjectHolder Return::Execute(Closure& closure, Context& context) {
	throw ReturnException(std::move(statement_->Execute(closure, context)));
	return {};
}

ClassDefinition::ClassDefinition(ObjectHolder cls)
: cls_(std::move(cls))
, cls_name_(cls_.TryAs<runtime::Class>()->GetName()) {
}

ObjectHolder ClassDefinition::Execute(Closure& closure, [[maybe_unused]] Context& context) {
	closure[cls_name_] = cls_;
	return closure.at(cls_name_);
}

// ----- FieldAssignment -----
FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
								 std::unique_ptr<Statement> rv)
	: object_(std::move(object))
	, field_name_(std::move(field_name))
	, rv_(std::move(rv)) {
}

ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
	auto obg = object_.Execute(closure, context);
	obg.TryAs<runtime::ClassInstance>()->Fields()[field_name_] = rv_->Execute(closure, context);
	return ObjectHolder::Share(*obg.TryAs<runtime::ClassInstance>()->Fields().at(field_name_).Get());
}

// ----- IfElse -----
IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
			   std::unique_ptr<Statement> else_body)
	: condition_(std::move(condition))
	, if_body_(std::move(if_body))
	, else_body_(std::move(else_body)) {
}

ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
	ObjectHolder condition = condition_->Execute(closure, context);
	if (!condition) {
		assert(false);
	}
	if (condition.TryAs<runtime::Bool>()->GetValue()) {
		if (ObjectHolder if_body = if_body_->Execute(closure, context)) {
			return if_body;
		}
	} else if (else_body_) {
		if (ObjectHolder else_body = else_body_->Execute(closure, context)) {
			return else_body;
		}
	}
	return {};
}

static bool IsTrue(const ObjectHolder& obj, Context& context) {
	return runtime::Equal(obj, ObjectHolder::Own(runtime::Bool(true)), context);
}

ObjectHolder Or::Execute(Closure& closure, Context& context) {
	if (IsTrue(lhs_->Execute(closure, context), context) ||
		IsTrue(rhs_->Execute(closure, context), context)) {
		return ObjectHolder::Own(runtime::Bool(true));
	}
	return ObjectHolder::Own(runtime::Bool(false));
}

ObjectHolder And::Execute(Closure& closure, Context& context) {
	if (IsTrue(lhs_->Execute(closure, context), context) &&
		IsTrue(rhs_->Execute(closure, context), context)) {
		return ObjectHolder::Own(runtime::Bool(true));
	}
	return ObjectHolder::Own(runtime::Bool(false));
}

ObjectHolder Not::Execute(Closure& closure, Context& context) {
	return ObjectHolder::Own(runtime::Bool(!argument_->Execute(closure, context).TryAs<runtime::Bool>()->GetValue()));
}

// ----- Comparison -----
Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
	: BinaryOperation(std::move(lhs), std::move(rhs))
	, cmp_(std::move(cmp)) {
}

ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
	bool result =  cmp_(lhs_->Execute(closure, context), rhs_->Execute(closure, context), context);
	return ObjectHolder::Own(runtime::Bool(result));
}

// ----- NewInstance -----
NewInstance::NewInstance(const runtime::Class& class_)
	: class_(class_) {
}

NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<Statement>> args)
	: class_(class_)
	, args_(std::move(args)) {
}

ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
	auto oh = ObjectHolder::Own(runtime::ClassInstance{ class_ });
	if (auto* class_inst = oh.TryAs<runtime::ClassInstance>()) {
		if (class_inst->HasMethod(INIT_METHOD, args_.size())) {
			std::vector<ObjectHolder> actual_args = GetActualArgs(args_, closure, context);
			class_inst->Call(INIT_METHOD, actual_args, context);
		}
	}
	return oh;
}

MethodBody::MethodBody(std::unique_ptr<Statement>&& body)
	: body_(std::move(body)) {
}

ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
	try {
		body_->Execute(closure, context);
	} catch (ReturnException& e) {
		return std::move(e.Result());
	}
	return {};
}

}  // namespace ast