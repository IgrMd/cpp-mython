#include "runtime.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

// ----- ObjectHolder -----
ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
	: data_(std::move(data)) {
}

void ObjectHolder::AssertIsValid() const {
	assert(data_ != nullptr);
}

ObjectHolder ObjectHolder::Share(Object& object) {
	// Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
	return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
}

ObjectHolder ObjectHolder::None() {
	return ObjectHolder();
}

Object& ObjectHolder::operator*() const {
	AssertIsValid();
	return *Get();
}

Object* ObjectHolder::operator->() const {
	AssertIsValid();
	return Get();
}

Object* ObjectHolder::Get() const {
	return data_.get();
}

ObjectHolder::operator bool() const {
	return Get() != nullptr;
}

// проверяет, содержится ли в object значение, приводимое к True
bool IsTrue(const ObjectHolder& object) {
	if (Bool* value_ptr = object.TryAs<Bool>()) {
		return value_ptr->GetValue();
	}
	if (Number* value_ptr = object.TryAs<Number>()) {
		return value_ptr->GetValue();
	}
	if (String* value_ptr = object.TryAs<String>()) {
		return !value_ptr->GetValue().empty();
	}
	return false;
}

// ----- ClassInstance -----
ClassInstance::ClassInstance(const Class& cls)
	: class_(cls) {
}

void ClassInstance::Print(std::ostream& os, [[maybe_unused]] Context& context) {
	if (HasMethod("__str__"s, 0)) {
		Call("__str__"s, {}, context)->Print(os, context);
	} else {
		os << this;
	}
}

bool ClassInstance::HasMethod(const std::string& method_name, size_t argument_count) const {
	if (const Method* method = class_.GetMethod(method_name)) {
		return method->formal_params.size() == argument_count;
	}
	return false;
}

Closure& ClassInstance::Fields() {
	return fields_;
}

const Closure& ClassInstance::Fields() const {
	return fields_;
}

ObjectHolder ClassInstance::Call(const std::string& method_name,
	const std::vector<ObjectHolder>& actual_args,
	Context& context) {
	const Method* method = class_.GetMethod(method_name);
	if (!method || method->formal_params.size() != actual_args.size()) {
		throw std::runtime_error("Not implemented"s);
	}
	Closure closure;
	for (size_t i = 0; i < actual_args.size(); ++i) {
		closure[method->formal_params[i]] = actual_args[i];
	}
	closure["self"s] = std::move(ObjectHolder::Share(*this));
	return method->body->Execute(closure, context);
}


// ----- Class -----
Class::Class(std::string name, std::vector<Method> methods, const Class* parent)
	: name_(std::move(name))
	, methods_(std::move(methods))
	, parent_(parent) {
}

const Method* Class::GetMethod(const std::string& name) const {
	auto method = std::find_if(
		methods_.cbegin(), methods_.cend(),
		[&name](const Method& m) { return m.name == name; }
	);
	if (method != methods_.cend()) {
		return &(*method);
	}
	if (parent_ != nullptr) {
		return parent_->GetMethod(name);
	}
	return nullptr;
}

[[nodiscard]] const std::string& Class::GetName() const {
	return name_;
}

void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
	os << "Class "sv << GetName();
}

void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
	os << (GetValue() ? "True"sv : "False"sv);
}

template<typename T>
static std::optional<bool> Equal(const ObjectHolder& lhs, const ObjectHolder& rhs) {
	if (lhs.TryAs<T>() && rhs.TryAs<T>()) {
		return lhs.TryAs<T>()->GetValue() == rhs.TryAs<T>()->GetValue();
	}
	return std::nullopt;
}

template<typename T>
static std::optional<bool> Less(const ObjectHolder& lhs, const ObjectHolder& rhs) {
	if (lhs.TryAs<T>() && rhs.TryAs<T>()) {
		return lhs.TryAs<T>()->GetValue() < rhs.TryAs<T>()->GetValue();
	}
	return std::nullopt;
}

bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
	if (!lhs && !rhs) {
		return true;
	}
	if (auto bool_result = Equal<Bool>(lhs, rhs)) {
		return bool_result.value();
	}
	if (auto number_result = Equal<Number>(lhs, rhs)) {
		return number_result.value();
	}
	if (auto string_result = Equal<String>(lhs, rhs)) {
		return string_result.value();
	}
	if (lhs.TryAs<ClassInstance>()) {
		return lhs.TryAs<ClassInstance>()->Call("__eq__", { rhs }, context).TryAs<Bool>()->GetValue();
	}
	throw std::runtime_error("Cannot compare objects for equality"s);
	return false;
}

bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
	if (auto bool_result = Less<Bool>(lhs, rhs)) {
		return bool_result.value();
	}
	if (auto number_result = Less<Number>(lhs, rhs)) {
		return number_result.value();
	}
	if (auto string_result = Less<String>(lhs, rhs)) {
		return string_result.value();
	}
	if (lhs.TryAs<ClassInstance>()) {
		return lhs.TryAs<ClassInstance>()->Call("__lt__", { rhs }, context).TryAs<Bool>()->GetValue();
	}
	throw std::runtime_error("Cannot compare objects for less"s);
	return false;
}

bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
	return !Equal(lhs, rhs, context);
}

bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
	return !Less(lhs, rhs, context) && !Equal(lhs, rhs, context);
}

bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
	return Less(lhs, rhs, context) || Equal(lhs, rhs, context);

}

bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
	return !Less(lhs, rhs, context);
}

}  // namespace runtime