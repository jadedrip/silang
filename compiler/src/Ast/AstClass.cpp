﻿#include "stdafx.h"
#include "ClassContext.h"
#include "AstDef.h"
#include "AstClass.h"
#include "AstFunction.h"
#include "AstProtected.h"
#include "AstDef.h"
#include "AstCall.h"
#include "AstConst.h"
#include "../Type/AutoType.h"
#include "../Type/ClassInstanceType.h"
#include "CodeGenerate/NewGen.h"
#include "CodeGenerate/DefGen.h"
#include "modules.h"

using namespace llvm;

CodeGen * AstClass::makeGen(AstContext * parent)
{
	parent->setClass(name, this);
	_parent = parent;

	for (auto i : block) {
		// 检测构造函数
		auto *p=dynamic_cast<AstFunction*>(i);
		if (p) {
			if (p->name == ":init") {
				if (_construstor)
					throw std::runtime_error("Dup construstor");
				_construstor=p;
			}
			continue;
		}	  
		// 检测是否模版类
		auto *q = dynamic_cast<AstDef*>(i);
		if (q) {
			if (!q->type || AstType::AutoTyId == q->type->type())
				_templated = true;
		}				

		// 是否常量
		auto *e = dynamic_cast<AstConst*>(i);
		if (e) {
			constValues[i->name] = i->makeGen(parent);
		}
	}

	return nullptr;
}

CodeGen * AstClass::makeNew(AstContext* parent, std::vector<std::pair<std::string, CodeGen*>>& args)
{						  
	llvm::LLVMContext &c = parent->context();
	NewGen* classObject = new NewGen();		// TODO: malloc

	if (_construstor) {
		AstFunction::OrderedParameters *ordered = _construstor->orderParameters(parent->context(), args);
		if (!args.empty() && !ordered)
			return nullptr;  // 不匹配
							 // 推导类
		auto a = generateClass(c, ordered);
		classObject->type = a->llvmType(c);

		classObject->construstor = _construstor->createCallGen(
			parent->context(),
			ordered->parameters,
			ordered->variableGen,
			classObject,
			a,
			a->creator
		);
	}else{
		auto a = generateClass(c, nullptr);
		auto t = classObject->type = a->llvmType(c);
		parent->setCompiledClass(t->getStructName(), a);
	}
		
	if (args.empty() || classObject->construstor) {
		return classObject;
	}
	throw std::runtime_error("类" + name + "没有合适的构造函数");
}

ClassInstanceType* AstClass::generateClass(llvm::LLVMContext& c, AstFunction::OrderedParameters *ordered)
{
	if (_generated) return _generated;

	// 如果是模板的，查看是否有已经生成的
	//if (_templated) {
	//	auto a=_cached.find(reinterpret_cast<intptr_t>(_construstor));
	//	if (a != _cached.end()) return a->second;
	//}

	std::string pathName = _parent->pathName + "." + name;

	bool isProctected = false;
	int idx = 0;
	std::vector< Type* > types;
	std::map< std::string, ClassMemberGen* > members;

	ClassInstanceType* cls = new ClassInstanceType(_parent->pathName, name );
	auto *context=cls->makeContext(_parent);

	for (auto i : block) {
		// 创建函数、变量索引
		auto x = dynamic_cast<AstFunction*>(i);
		if (x) {
			// 函数在被调用时才固化
			// TODO: 貌似应该复制一份？
			x->_parent = context;
			cls->methds.insert(std::make_pair(x->name, x));
			continue;
		}

		std::vector< CodeGen* > initGen;
		auto v = dynamic_cast<AstDef*>(i);
		if (v) {
			// 首先确定变量类型
			Type* p = v->type->isAuto() ? nullptr : v->type->llvmType(c);

			for (auto a : v->vars) {
				if (!p) {  // 如果是自动类型
					auto valueGen=a.second->makeGen(context);
					p = valueGen->type;
					if (!p) throw std::runtime_error("Unknown member's type:" + a.first);
				}
				types.push_back(p);
				ClassMemberGen* m = new ClassMemberGen();
				m->name = v->name;
				m->isProtected = isProctected;
				m->type = p;
				m->index = idx++;
				cls->memberGens[a.first] = m;
				cls->defaultValues[a.first] = a.second->makeGen(_parent);
			}
			continue;
		}

		auto y = dynamic_cast<AstProtected*>(i);
		if(y){
			isProctected = true;
			continue;
		}

		auto c = dynamic_cast<AstConst*>(i);
		if (c) {
			continue;
		}

		throw std::runtime_error("类内有未知的定义");
	}

	for (auto i : constValues) {
		context->setSymbolValue(i.first, i.second);
	}

	//
	std::string u = _parent->pathName;

// 优先使用 C 里面定义的对象
	for (auto &c : u) {
		if (c == '.') c = '_';
	}
	cls->_type = CLangModule::getStruct(u, name);
	if (!cls->_type) {
		cls->_type = StructType::create(context->context(), types, u+"_"+name);
	}

	// 构造函数
	cls->creator= _construstor ?
		_construstor->getFunctionInstance(c, ordered->parameters, ordered->variableGen, cls) :
		nullptr;
	if (!_templated) {
		_generated = cls;
	}
	return cls;
}




