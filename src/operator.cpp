#include "compiler.h"
#include "types.h"
#include "tokens.h"
#include "jitlinker.h"
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/Linker/Linker.h>


TypedValue* Compiler::compAdd(TypedValue *l, TypedValue *r, BinOpNode *op){
    switch(l->type->type){
        case TT_I8:  case TT_U8:  case TT_C8:
        case TT_I16: case TT_U16:
        case TT_I32: case TT_U32:
        case TT_I64: case TT_U64:
        case TT_Ptr:
            return new TypedValue(builder.CreateAdd(l->val, r->val), l->type);
        case TT_F16:
        case TT_F32:
        case TT_F64:
            return new TypedValue(builder.CreateFAdd(l->val, r->val), l->type);

        default:
            return compErr("binary operator + is undefined for the type " + typeNodeToColoredStr(l->type) + " and " + typeNodeToColoredStr(r->type), op->loc);
    }
}

TypedValue* Compiler::compSub(TypedValue *l, TypedValue *r, BinOpNode *op){
    switch(l->type->type){
        case TT_I8:  case TT_U8:  case TT_C8:
        case TT_I16: case TT_U16:
        case TT_I32: case TT_U32:
        case TT_I64: case TT_U64:
        case TT_Ptr:
            return new TypedValue(builder.CreateSub(l->val, r->val), l->type);
        case TT_F16:
        case TT_F32:
        case TT_F64:
            return new TypedValue(builder.CreateFSub(l->val, r->val), l->type);

        default:
            return compErr("binary operator - is undefined for the type " + typeNodeToColoredStr(l->type) + " and " + typeNodeToColoredStr(r->type), op->loc);
    }
}

TypedValue* Compiler::compMul(TypedValue *l, TypedValue *r, BinOpNode *op){
    switch(l->type->type){
        case TT_I8:  case TT_U8:  case TT_C8:
        case TT_I16: case TT_U16:
        case TT_I32: case TT_U32:
        case TT_I64: case TT_U64:
            return new TypedValue(builder.CreateMul(l->val, r->val), l->type);
        case TT_F16:
        case TT_F32:
        case TT_F64:
            return new TypedValue(builder.CreateFMul(l->val, r->val), l->type);

        default:
            return compErr("binary operator * is undefined for the type " + typeNodeToColoredStr(l->type) + " and " + typeNodeToColoredStr(r->type), op->loc);
    }
}

TypedValue* Compiler::compDiv(TypedValue *l, TypedValue *r, BinOpNode *op){
    switch(l->type->type){
        case TT_I8:  
        case TT_I16: 
        case TT_I32: 
        case TT_I64: 
            return new TypedValue(builder.CreateSDiv(l->val, r->val), l->type);
        case TT_U8: case TT_C8:
        case TT_U16:
        case TT_U32:
        case TT_U64:
            return new TypedValue(builder.CreateUDiv(l->val, r->val), l->type);
        case TT_F16:
        case TT_F32:
        case TT_F64:
            return new TypedValue(builder.CreateFDiv(l->val, r->val), l->type);

        default: 
            return compErr("binary operator / is undefined for the type " + typeNodeToColoredStr(l->type) + " and " + typeNodeToColoredStr(r->type), op->loc);
    }
}

TypedValue* Compiler::compRem(TypedValue *l, TypedValue *r, BinOpNode *op){
    switch(l->type->type){
        case TT_I8: 
        case TT_I16:
        case TT_I32:
        case TT_I64:
            return new TypedValue(builder.CreateSRem(l->val, r->val), l->type);
        case TT_U8: case TT_C8:
        case TT_U16:
        case TT_U32:
        case TT_U64:
            return new TypedValue(builder.CreateURem(l->val, r->val), l->type);
        case TT_F16:
        case TT_F32:
        case TT_F64:
            return new TypedValue(builder.CreateFRem(l->val, r->val), l->type);

        default:
            return compErr("binary operator % is undefined for the types " + typeNodeToColoredStr(l->type) + " and " + typeNodeToColoredStr(r->type), op->loc);
    }
}

/*
 *  Compiles the extract operator, [
 */
TypedValue* Compiler::compExtract(TypedValue *l, TypedValue *r, BinOpNode *op){
    if(!isIntTypeTag(r->type->type)){
        return compErr("Index of operator '[' must be an integer expression, got expression of type " + typeNodeToColoredStr(r->type), op->loc);
    }

    if(l->type->type == TT_Array){
        //check for alloca
        if(LoadInst *li = dyn_cast<LoadInst>(l->val)){
            Value *arr = li->getPointerOperand();
            
            vector<Value*> indices;
            indices.push_back(ConstantInt::get(*ctxt, APInt(64, 0, true)));
            indices.push_back(r->val);
            return new TypedValue(builder.CreateLoad(builder.CreateGEP(arr, indices)), l->type->extTy);
        }else{
            return new TypedValue(builder.CreateExtractElement(l->val, r->val), l->type->extTy.get());
        }
    }else if(l->type->type == TT_Ptr){
        return new TypedValue(builder.CreateLoad(builder.CreateGEP(l->val, r->val)), l->type->extTy);

    }else if(l->type->type == TT_Tuple || l->type->type == TT_Data){
		auto indexval = dyn_cast<ConstantInt>(r->val);
        if(!indexval)
            return compErr("Tuple indices must always be known at compile time.", op->loc);

        auto index = indexval->getZExtValue();

        //get the type from the index in question
        TypeNode* indexTyn = l->type->extTy.get();

        if(!indexTyn){
            auto *dataty = lookupType(l->type->typeName);
            if(!dataty)
                return compErr("Error when attempting to index variable of type " + typeNodeToColoredStr(l->type), op->loc);

            indexTyn = dataty->tyn->extTy.get();
        }

        if(index >= getTupleSize(indexTyn))
            return compErr("Index of " + to_string(index) + " exceeds number of fields in " + typeNodeToColoredStr(l->type), op->loc);

        for(unsigned i = 0; i < index; i++)
            indexTyn = (TypeNode*)indexTyn->next.get();

        Value *tup = llvmTypeToTypeTag(l->getType()) == TT_Ptr ? builder.CreateLoad(l->val) : l->val;
        return new TypedValue(builder.CreateExtractValue(tup, index), copy(indexTyn));
    }
    return compErr("Type " + typeNodeToColoredStr(l->type) + " does not have elements to access", op->loc);
}


/*
 *  Compiles an insert statement for arrays or tuples.
 *  An insert statement would look similar to the following (in ante syntax):
 *
 *  i32,i32,i32 tuple = (1, 2, 4)
 *  tuple#2 = 3
 *
 *  This method Works on lvals and returns a void value.
 */
TypedValue* Compiler::compInsert(BinOpNode *op, Node *assignExpr){
    auto *tmp = op->lval->compile(this);

    //if(!dynamic_cast<LoadInst*>(tmp->val))
    if(!tmp->hasModifier(Tok_Mut))
        return compErr("Variable must be mutable to insert values, but instead is an immutable " +
                typeNodeToColoredStr(tmp->type), op->lval->loc);

    Value *var = static_cast<LoadInst*>(tmp->val)->getPointerOperand();

    auto *index = op->rval->compile(this);
    auto *newVal = assignExpr->compile(this);

    //see if insert operator # = is overloaded already
    string basefn = "#";
    string mangledfn = mangle(basefn, tmp->type.get(), mkAnonTypeNode(TT_I32), newVal->type.get());
    auto *fn = getFunction(basefn, mangledfn);
    if(fn){
        vector<Value*> args = {var, index->val, newVal->val};
        return new TypedValue(builder.CreateCall(fn->val, args), fn->type->extTy);
    }

    switch(tmp->type->type){
        case TT_Array: {
            if(!typeEq(tmp->type->extTy.get(), newVal->type.get()))
                return compErr("Cannot create store of types: "+typeNodeToColoredStr(tmp->type)+" <- "
                        +typeNodeToColoredStr(newVal->type), assignExpr->loc);

            Value *cast = builder.CreateBitCast(var, var->getType()->getPointerElementType()->getArrayElementType()->getPointerTo());
            Value *dest = builder.CreateInBoundsGEP(cast, index->val);
            builder.CreateStore(newVal->val, dest);
            return getVoidLiteral();
        }
        case TT_Ptr: {
            if(!typeEq(tmp->type->extTy.get(), newVal->type.get()))
                return compErr("Cannot create store of types: "+typeNodeToColoredStr(tmp->type)+" <- "
                        +typeNodeToColoredStr(newVal->type), assignExpr->loc);

            Value *dest = builder.CreateInBoundsGEP(/*tmp->getType()->getPointerElementType(),*/ tmp->val, index->val);
            builder.CreateStore(newVal->val, dest);
            return getVoidLiteral();
        }
        case TT_Tuple: case TT_Data: {
            ConstantInt *tupIndexVal = dyn_cast<ConstantInt>(index->val);
            if(!tupIndexVal){
                return compErr("Tuple indices must always be known at compile time.", op->loc);
            }else{
                auto tupIndex = tupIndexVal->getZExtValue();

                //Type of element at tuple index tupIndex, for type checking
                auto* tupIndexTy = (TypeNode*)getNthNode(tmp->type->extTy.get(), tupIndex);
                auto* exprTy = newVal->type.get();

                if(!typeEq(tupIndexTy, exprTy)){
                    return compErr("Cannot assign expression of type " + typeNodeToColoredStr(exprTy)
                                + " to tuple index " + to_string(tupIndex) + " of type " + typeNodeToColoredStr(tupIndexTy),
                                assignExpr->loc);
                }

                auto *ins = builder.CreateInsertValue(tmp->val, newVal->val, tupIndex);
                builder.CreateStore(ins, var);
                return getVoidLiteral();//new TypedValue(builder.CreateStore(insertedTup, var), mkAnonTypeNode(TT_Void));
            }
        }
        default:
            return compErr("Variable being indexed must be an Array or Tuple, but instead is a(n) " +
                    typeNodeToColoredStr(tmp->type), op->loc); }
}


TypedValue* createUnionVariantCast(Compiler *c, TypedValue *valToCast, TypeNode *castTyn, DataType *dataTy, TypeCheckResult &tyeq){
    auto *unionDataTy = c->lookupType(dataTy->getParentUnionName());

    auto dtcpy = copy(unionDataTy->tyn);
    dtcpy->type = TT_TaggedUnion;
    dtcpy->typeName = dataTy->getParentUnionName();
    if(tyeq.res == TypeCheckResult::SuccessWithTypeVars){
        bindGenericToType(dtcpy, tyeq.bindings);
    }

    auto t = unionDataTy->getTagVal(castTyn->typeName);
    Type *variantTy = c->typeNodeToLlvmType(valToCast->type.get());

    vector<Type*> unionTys;
    unionTys.push_back(Type::getInt8Ty(*c->ctxt));
    unionTys.push_back(variantTy);

    vector<Constant*> unionVals;
    unionVals.push_back(ConstantInt::get(*c->ctxt, APInt(8, t, true))); //tag
    unionVals.push_back(UndefValue::get(variantTy));


    Type *unionTy = c->typeNodeToLlvmType(dtcpy);

    //create a struct of (u8 tag, <union member type>)
    auto *uninitUnion = ConstantStruct::get(StructType::get(*c->ctxt, unionTys), unionVals);
    auto* taggedUnion = c->builder.CreateInsertValue(uninitUnion, valToCast->val, 1);

    //allocate for the largest possible union member
    auto *alloca = c->builder.CreateAlloca(unionTy);

    //but bitcast it the the current member
    auto *castTo = c->builder.CreateBitCast(alloca, unionTy->getPointerTo());
    c->builder.CreateStore(taggedUnion, castTo);

    //load the original alloca, not the bitcasted one
    Value *unionVal = c->builder.CreateLoad(alloca);

    return new TypedValue(unionVal, dtcpy);
}


string getCastFnBaseName(TypeNode *t){
    return (t->params.empty() ? typeNodeToStr(t) : t->typeName) + "_init";
}


TypedValue* compMetaFunctionResult(Compiler *c, LOC_TY &loc, string &baseName, string &mangledName, vector<TypedValue*> &typedArgs);

/*
 *  Creates a cast instruction appropriate for valToCast's type to castTy.
 */
TypedValue* createCast(Compiler *c, TypeNode *castTyn, TypedValue *valToCast){
    //first, see if the user created their own cast function
    if(TypedValue *fn = c->getCastFn(valToCast->type.get(), castTyn)){
        vector<Value*> args;
        if(valToCast->type->type != TT_Void) args.push_back(valToCast->val);

        if(fn->type->type == TT_MetaFunction){
            string baseName = getCastFnBaseName(castTyn);
            string mangledName = mangle(baseName, valToCast->type.get());
            vector<TypedValue*> args = {valToCast};
            return compMetaFunctionResult(c, castTyn->loc, baseName, mangledName, args);
        }

        auto *call = c->builder.CreateCall(fn->val, args);
        return new TypedValue(call, fn->type->extTy);
    }

    //otherwise, fallback on known conversions
    if(isIntTypeTag(castTyn->type)){
        Type *castTy = c->typeNodeToLlvmType(castTyn);
        
        // int -> int  (maybe unsigned)
        if(isIntTypeTag(valToCast->type->type)){
            return new TypedValue(c->builder.CreateIntCast(valToCast->val, castTy, isUnsignedTypeTag(castTyn->type)), castTyn);

        // float -> int
        }else if(isFPTypeTag(valToCast->type->type)){
            if(isUnsignedTypeTag(castTyn->type)){
                return new TypedValue(c->builder.CreateFPToUI(valToCast->val, castTy), castTyn);
            }else{
                return new TypedValue(c->builder.CreateFPToSI(valToCast->val, castTy), castTyn);
            }

        // ptr -> int
        }else if(valToCast->type->type == TT_Ptr){
            return new TypedValue(c->builder.CreatePtrToInt(valToCast->val, castTy), castTyn);
        }
    }else if(isFPTypeTag(castTyn->type)){
        Type *castTy = c->typeNodeToLlvmType(castTyn);

        // int -> float
        if(isIntTypeTag(valToCast->type->type)){
            if(isUnsignedTypeTag(valToCast->type->type)){
                return new TypedValue(c->builder.CreateUIToFP(valToCast->val, castTy), castTyn);
            }else{
                return new TypedValue(c->builder.CreateSIToFP(valToCast->val, castTy), castTyn);
            }

        // float -> float
        }else if(isFPTypeTag(valToCast->type->type)){
            return new TypedValue(c->builder.CreateFPCast(valToCast->val, castTy), castTyn);
        }

    }else if(castTyn->type == TT_Ptr){
        Type *castTy = c->typeNodeToLlvmType(castTyn);

        // ptr -> ptr
        if(valToCast->type->type == TT_Ptr){
            return new TypedValue(c->builder.CreatePointerCast(valToCast->val, castTy), castTyn);

		// int -> ptr
        }else if(isIntTypeTag(valToCast->type->type)){
            return new TypedValue(c->builder.CreateIntToPtr(valToCast->val, castTy), castTyn);
        }
    }

    //if all automatic checks fail, test for structural equality in a datatype cast!
    //This would apply for the following scenario (and all structurally equivalent types)
    //
    //type Int = i32
    //let example = Int 3
    //              ^^^^^
    auto *dataTy = c->lookupType(castTyn->typeName);
    TypeCheckResult tyeq{false};

    if(dataTy && !!(tyeq = c->typeEq(valToCast->type.get(), dataTy->tyn.get()))){
        //check if this is a tagged union (sum type)
        if(dataTy->isUnionTag())
            return createUnionVariantCast(c, valToCast, castTyn, dataTy, tyeq);

        auto *tycpy = copy(valToCast->type);
        tycpy->typeName = castTyn->typeName;
        tycpy->type = TT_Data;

        return new TypedValue(valToCast->val, tycpy);
    //test for the reverse case, something like:  i32 example
    //where example is of type Int
    }else if(valToCast->type->typeName.size() > 0 && (dataTy = c->lookupType(valToCast->type->typeName))){
        if(!!c->typeEq(dataTy->tyn.get(), castTyn)){
            auto *tycpy = copy(valToCast->type);
            tycpy->typeName = "";
            tycpy->type = castTyn->type;
            return new TypedValue(valToCast->val, tycpy);
        }
    }
 
    return nullptr;
}

TypedValue* TypeCastNode::compile(Compiler *c){
    auto *rtval = rval->compile(c);

    auto *ty = copy(typeExpr);
    c->searchAndReplaceBoundTypeVars(ty);

    for(auto &p : ty->params){
        if(p->type == TT_TypeVar){
            Variable *v = c->lookup(p->typeName);
            if(!v) c->compErr("Unbound typevar "+p->typeName, p->loc);

            auto zext = dyn_cast<ConstantInt>(v->tval->val)->getZExtValue();
            p.reset((TypeNode*)zext);
        }
    }

    auto* tval = createCast(c, ty, rtval);

    if(!tval){
        //if(!!c->typeEq(rtval->type.get(), ty))
        //    c->compErr("Typecast to same type", loc, ErrorType::Warning);
            
        return c->compErr("Invalid type cast " + typeNodeToColoredStr(rtval->type) + 
                " -> " + typeNodeToColoredStr(ty), loc);
    }
    return tval;
}

TypedValue* compIf(Compiler *c, IfNode *ifn, BasicBlock *mergebb, vector<pair<TypedValue*,BasicBlock*>> &branches){
    auto *cond = ifn->condition->compile(c);

    if(cond->type->type != TT_Bool)
        return c->compErr("If condition must be of type " + typeNodeToColoredStr(mkAnonTypeNode(TT_Bool)) +
                    " but an expression of type " + typeNodeToColoredStr(cond->type.get()) + " was given", ifn->condition->loc);
    
    Function *f = c->builder.GetInsertBlock()->getParent();
    auto &blocks = f->getBasicBlockList();

    auto *thenbb = BasicBlock::Create(*c->ctxt, "then");
   
    //only create the else block if this ifNode actually has an else clause
    BasicBlock *elsebb = 0;
    
    if(ifn->elseN){
        if(dynamic_cast<IfNode*>(ifn->elseN.get())){
            elsebb = BasicBlock::Create(*c->ctxt, "elif");
            c->builder.CreateCondBr(cond->val, thenbb, elsebb);
    
            blocks.push_back(thenbb);
            c->builder.SetInsertPoint(thenbb);
            auto *thenVal = ifn->thenN->compile(c);

            //If a break, continue, or return was encountered then this branch doesn't merge to the endif
            if(!dyn_cast<ReturnInst>(thenVal->val) and !dyn_cast<BranchInst>(thenVal->val)){
                auto *thenretbb = c->builder.GetInsertBlock();
                c->builder.CreateBr(mergebb);
            
                //save the 'then' value for the PhiNode after all the elifs
                branches.push_back({thenVal, thenretbb});

                blocks.push_back(elsebb);
            }

            c->builder.SetInsertPoint(elsebb);
            return compIf(c, (IfNode*)ifn->elseN.get(), mergebb, branches);
        }else{
            elsebb = BasicBlock::Create(*c->ctxt, "else");
            c->builder.CreateCondBr(cond->val, thenbb, elsebb);

            blocks.push_back(thenbb);
            blocks.push_back(elsebb);
            blocks.push_back(mergebb);
        }
    }else{
        c->builder.CreateCondBr(cond->val, thenbb, mergebb);
        blocks.push_back(thenbb);
        blocks.push_back(mergebb);
    }

    c->builder.SetInsertPoint(thenbb);
    auto *thenVal = ifn->thenN->compile(c);
    if(!thenVal) return 0;
    auto *thenretbb = c->builder.GetInsertBlock(); //bb containing final ret of then branch.


    if(!dyn_cast<ReturnInst>(thenVal->val) and !dyn_cast<BranchInst>(thenVal->val))
        c->builder.CreateBr(mergebb);

    if(ifn->elseN){
        //save the final 'then' value for the upcoming PhiNode
        branches.push_back({thenVal, thenretbb});

        c->builder.SetInsertPoint(elsebb);
        auto *elseVal = ifn->elseN->compile(c);
        auto *elseretbb = c->builder.GetInsertBlock();

        if(!elseVal) return 0;

        //save the final else
        if(!dyn_cast<ReturnInst>(elseVal->val) and !dyn_cast<BranchInst>(elseVal->val))
            branches.push_back({elseVal, elseretbb});

        if(!thenVal) return 0;

        auto eq = c->typeEq(thenVal->type.get(), elseVal->type.get());
        if(!eq and !dyn_cast<ReturnInst>(thenVal->val) and !dyn_cast<ReturnInst>(elseVal->val) and
                   !dyn_cast<BranchInst>(thenVal->val) and !dyn_cast<BranchInst>(elseVal->val)){

            bool tEmpty = thenVal->type->params.empty();
            bool eEmpty = elseVal->type->params.empty();

            //TODO: copy type
            if(tEmpty and not eEmpty){
                bindGenericToType(thenVal->type.get(), elseVal->type->params);
                thenVal->val->mutateType(c->typeNodeToLlvmType(thenVal->type.get()));

                if(LoadInst *li = dyn_cast<LoadInst>(thenVal->val)){
                    auto *alloca = li->getPointerOperand();
                    auto *cast = c->builder.CreateBitCast(alloca, c->typeNodeToLlvmType(elseVal->type.get())->getPointerTo());
                    thenVal->val = c->builder.CreateLoad(cast);
                }
            }else if(eEmpty and not tEmpty){
                bindGenericToType(elseVal->type.get(), thenVal->type->params);
                elseVal->val->mutateType(c->typeNodeToLlvmType(elseVal->type.get()));
                
                if(LoadInst *ri = dyn_cast<LoadInst>(elseVal->val)){
                    auto *alloca = ri->getPointerOperand();
                    auto *cast = c->builder.CreateBitCast(alloca, c->typeNodeToLlvmType(thenVal->type.get())->getPointerTo());
                    elseVal->val = c->builder.CreateLoad(cast);
                }
            }else{
                return c->compErr("If condition's then expr's type " + typeNodeToColoredStr(thenVal->type) +
                            " does not match the else expr's type " + typeNodeToColoredStr(elseVal->type), ifn->loc);
            }
        }
        
        if(eq.res == TypeCheckResult::SuccessWithTypeVars){
            bool tEmpty = thenVal->type->params.empty();
            bool eEmpty = elseVal->type->params.empty();
           
            TypedValue *generic;
            TypedValue *concrete;

            if(tEmpty and !eEmpty){
                generic = thenVal;
                concrete = elseVal;
            }else if(eEmpty and !tEmpty){
                generic = elseVal;
                concrete = thenVal;
            }else{
                return c->compErr("If condition's then expr's type " + typeNodeToColoredStr(thenVal->type) +
                            " does not match the else expr's type " + typeNodeToColoredStr(elseVal->type), ifn->loc);
            }
            
            //TODO: copy type
            bindGenericToType(generic->type.get(), concrete->type->params);
            generic->val->mutateType(c->typeNodeToLlvmType(generic->type.get()));

            auto *ri = dyn_cast<ReturnInst>(generic->val);

            if(LoadInst *li = dyn_cast<LoadInst>(ri ? ri->getReturnValue() : generic->val)){
                auto *alloca = li->getPointerOperand();

                auto *ins = ri ? ri->getParent() : c->builder.GetInsertBlock();
                c->builder.SetInsertPoint(ins);

                auto *cast = c->builder.CreateBitCast(alloca, c->typeNodeToLlvmType(generic->type.get())->getPointerTo());
                auto *fixed_ret = c->builder.CreateLoad(cast);
                generic->val = fixed_ret;
                if(ri) ri->eraseFromParent();
            }
        }
        
        if(!dyn_cast<ReturnInst>(elseVal->val) and !dyn_cast<BranchInst>(elseVal->val))
            c->builder.CreateBr(mergebb);

        c->builder.SetInsertPoint(mergebb);

        //finally, create the ret value of this if expr, unless it is of void type
        if(thenVal->type->type != TT_Void){
            auto *phi = c->builder.CreatePHI(thenVal->getType(), branches.size());

            for(auto &pair : branches)
                if(!dyn_cast<ReturnInst>(pair.first->val)){
                    phi->addIncoming(pair.first->val, pair.second);
                }

            return new TypedValue(phi, thenVal->type);
        }else{
            return c->getVoidLiteral();
        }
    }else{
        c->builder.SetInsertPoint(mergebb);
        return c->getVoidLiteral();
    }
}

TypedValue* IfNode::compile(Compiler *c){
    auto branches = vector<pair<TypedValue*,BasicBlock*>>();
    auto *mergebb = BasicBlock::Create(*c->ctxt, "endif");
    return compIf(c, this, mergebb, branches);
}


TypedValue* Compiler::compMemberAccess(Node *ln, VarNode *field, BinOpNode *binop){
    if(!ln) return 0;

    if(auto *tn = dynamic_cast<TypeNode*>(ln)){
        //since ln is a typenode, this is a static field/method access, eg Math.rand
        string valName = typeNodeToStr(tn) + "_" + field->name;

        auto& l = getFunctionList(valName);

        if(l.size() == 1){
            auto& fd = l.front();
            if(!fd->tv)
                fd->tv = compFn(fd.get());

            return fd->tv;
        }else if(l.size() > 1){
            compErr("Multiple static methods of the same name with different parameters are currently unimplemented.  In the mean time, you can use global functions.", field->loc);
            for(auto &fd : l)
                compErr("Candidate function", fd->fdn->loc, ErrorType::Note);
            return 0;
        }

        return compErr("No static method called '" + field->name + "' was found in type " + 
                typeNodeToColoredStr(tn), binop->loc);
    }else{
        //ln is not a typenode, so this is not a static method call
        Value *val;
        TypeNode *ltyn;
        TypeNode *tyn;

        //prevent l from being used after this scope; only val and tyn should be used as only they
        //are updated with the automatic pointer dereferences.
        { 
            auto *l = ln->compile(this);
            if(!l) return 0;

            val = l->val;
            tyn = ltyn = l->type.get();
        }

        //the . operator automatically dereferences pointers, so update val and tyn accordingly.
        while(tyn->type == TT_Ptr){
            val = builder.CreateLoad(val);
            tyn = tyn->extTy.get();
        }

        //if pointer derefs took place, tyn could have lost its modifiers, so make sure they are copied back
        if(ltyn->type == TT_Ptr and tyn->modifiers.empty())
            tyn->copyModifiersFrom(ltyn);

        //check to see if this is a field index
        if(tyn->type == TT_Data || tyn->type == TT_Tuple){
            auto dataTy = lookupType(typeNodeToStr(tyn));

            if(dataTy){
                auto index = dataTy->getFieldIndex(field->name);

                if(index != -1){
                    TypeNode *indexTy = dataTy->tyn->extTy.get();

                    for(int i = 0; i < index; i++)
                        indexTy = (TypeNode*)indexTy->next.get();

                    //The data type when looking up (usually) does not have any modifiers,
                    //so apply any potential modifers from the parent to this
                    if(indexTy->modifiers.empty())
                        indexTy->copyModifiersFrom(tyn);

                    return new TypedValue(builder.CreateExtractValue(val, index), copy(indexTy));
                }
            }
        }

        //not a field, so look for a method.
        //TODO: perhaps create a calling convention function
        string funcName = typeNodeToStr(tyn) + "_" + field->name;
        auto& l = getFunctionList(funcName);

        if(l.size() == 1){
            auto& fd = l.front();
            if(!fd->tv){
                fd->tv = compFn(fd.get());
                if(!fd->tv) return 0; //error when compiling function
            }

            TypedValue *obj = new TypedValue(val, copy(tyn));
            auto *method_fn = new TypedValue(fd->tv->val, fd->tv->type);
            return new MethodVal(obj, method_fn);
        }else if(l.size() > 1){
            compErr("Multiple methods of the same name with different parameters are currently unimplemented.  In the mean time, you can use global functions.", field->loc);
            for(auto &fd : l)
                compErr("Candidate function", fd->fdn->loc, ErrorType::Note);
            
            return 0;
        }else
            return compErr("Method/Field " + field->name + " not found in type " + typeNodeToColoredStr(tyn), binop->loc);
    }
}


template<typename T>
void push_front(vector<T*> *vec, T *val){
    vector<T*> cpy;
    cpy.push_back(val);

    for(auto *v : *vec)
        cpy.push_back(v);

    *vec = cpy;
}


vector<TypeNode*> toTypeNodeVector(vector<TypedValue*> &tvs){
    vector<TypeNode*> ret;
    for(auto *tv : tvs){
        ret.push_back(tv->type.get());
    }
    return ret;
}

//ante function to convert between IEEE half and IEEE single
//since c++ does not support an IEEE half value
#ifndef F16_BOOT
extern "C" float f32_from_f16(float f);
#else
float f32_from_f16(float f) {
    return f;
}
#endif

/*
 *  Converts an llvm GenericValue to a TypedValue
 */
TypedValue* genericValueToTypedValue(Compiler *c, GenericValue gv, TypeNode *tn){
    auto *copytn = copy(tn);
    switch(tn->type){
        case TT_I8:              return new TypedValue(c->builder.getInt8( *gv.IntVal.getRawData()),    copytn);
        case TT_I16:             return new TypedValue(c->builder.getInt16(*gv.IntVal.getRawData()),    copytn);
        case TT_I32:             return new TypedValue(c->builder.getInt32(*gv.IntVal.getRawData()),    copytn);
        case TT_I64:             return new TypedValue(c->builder.getInt64(*gv.IntVal.getRawData()),    copytn);
        case TT_U8:              return new TypedValue(c->builder.getInt8( *gv.IntVal.getRawData()),    copytn);
        case TT_U16:             return new TypedValue(c->builder.getInt16(*gv.IntVal.getRawData()),    copytn);
        case TT_U32:             return new TypedValue(c->builder.getInt32(*gv.IntVal.getRawData()),    copytn);
        case TT_U64:             return new TypedValue(c->builder.getInt64(*gv.IntVal.getRawData()),    copytn);
        case TT_Isz:             return new TypedValue(c->builder.getInt64(*gv.IntVal.getRawData()),    copytn);
        case TT_Usz:             return new TypedValue(c->builder.getInt64(*gv.IntVal.getRawData()),    copytn);
        case TT_C8:              return new TypedValue(c->builder.getInt8( *gv.IntVal.getRawData()),    copytn);
        case TT_C32:             return new TypedValue(c->builder.getInt32(*gv.IntVal.getRawData()),    copytn);
        case TT_F16:             return new TypedValue(ConstantFP::get(*c->ctxt, APFloat(f32_from_f16(gv.FloatVal))),  copytn);
        case TT_F32:             return new TypedValue(ConstantFP::get(*c->ctxt, APFloat(gv.FloatVal)),  copytn);
        case TT_F64:             return new TypedValue(ConstantFP::get(*c->ctxt, APFloat(gv.DoubleVal)), copytn);
        case TT_Bool:            return new TypedValue(c->builder.getInt1(*gv.IntVal.getRawData()),     copytn);
        case TT_Tuple:           break;
        case TT_Array:           break;
        case TT_Ptr: {
            auto *cint = c->builder.getInt64((unsigned long) gv.PointerVal);
            auto *ty = c->typeNodeToLlvmType(tn);
            return new TypedValue(c->builder.CreateIntToPtr(cint, ty), copytn);
        }case TT_Data:
        case TT_TypeVar:
        case TT_Function:
        case TT_Method:
        case TT_TaggedUnion:
        case TT_MetaFunction:
        case TT_Type:
                                 break;
        case TT_Void:
            return c->getVoidLiteral();
    }
    
    c->errFlag = true;
    cerr << "genericValueToTypedValue: Unknown TypeTag " << typeTagToStr(tn->type) << endl;
    return 0;
}

/*
 *  Converts a TypedValue to an llvm GenericValue
 *  - Assumes the Value* within the TypedValue is a Constant*
 */
GenericValue typedValueToGenericValue(Compiler *c, TypedValue *tv){
    GenericValue ret;
    TypeTag tt = tv->type->type;

    switch(tt){
        case TT_I8:
        case TT_I16:
        case TT_I32:
        case TT_I64:
        case TT_U8:
        case TT_U16:
        case TT_U32:
        case TT_U64:
        case TT_Isz:
        case TT_Usz:;
        case TT_C8:
        case TT_C32:
        case TT_Bool: {
            auto *ci = dyn_cast<ConstantInt>(tv->val);
            if(!ci) break;
            ret.IntVal = APInt(getBitWidthOfTypeTag(tt), isUnsignedTypeTag(tt) ? ci->getZExtValue() : ci->getSExtValue());
            return ret;
        }
        case TT_F16:
        case TT_F32:
        case TT_F64:
        case TT_Tuple:
        case TT_Array:
        case TT_Ptr:
        case TT_Data:
        case TT_TypeVar:
        case TT_Function:
        case TT_Method:
        case TT_TaggedUnion:
        case TT_MetaFunction:
        case TT_Type:
        case TT_Void:
            break;
    }
    
    cerr << AN_ERR_COLOR << "error: " << AN_CONSOLE_RESET << "Compile-time function argument must be constant.\n";
    return GenericValue(nullptr);
}


vector<GenericValue> typedValuesToGenericValues(Compiler *c, vector<TypedValue*> &typedArgs, LOC_TY loc, string fnname){
    vector<GenericValue> ret;
    ret.reserve(typedArgs.size());

    for(size_t i = 0; i < typedArgs.size(); i++){
        auto *tv = typedArgs[i];

        if(!dyn_cast<Constant>(tv->val)){
            c->compErr("Parameter " + to_string(i+1) + " of metafunction " + fnname + " is not a compile time constant", loc);
            return ret;
        }
        ret.push_back(typedValueToGenericValue(c, tv));
    }
    return ret;
}



string getName(Node *n){
    if(VarNode *vn = dynamic_cast<VarNode*>(n))
        return vn->name;
    else if(BinOpNode *op = dynamic_cast<BinOpNode*>(n))
        return getName(op->lval.get()) + "_" + getName(op->rval.get());
    else if(TypeNode *tn = dynamic_cast<TypeNode*>(n))
        return tn->params.empty() ? typeNodeToStr(tn) : tn->typeName;
    else
        return "";
}



extern map<string, CtFunc*> compapi;
/*
 *  Compile a compile-time function/macro which should not return a function call, just a compile-time constant.
 *  Ex: A call to Ante.getAST() would be a meta function as it wouldn't make sense to get the parse tree
 *      during runtime
 *
 *  - Assumes arguments are already type-checked
 */
TypedValue* compMetaFunctionResult(Compiler *c, LOC_TY &loc, string &baseName, string &mangledName, vector<TypedValue*> &typedArgs){
    CtFunc* fn;
    if((fn = compapi[baseName])){
        void *res;
        GenericValue gv;

        //TODO organize CtFunc's by param count + type instead of a hard-coded name check
        if(baseName == "Ante_debug"){
            if(typedArgs.size() != 1)
                return c->compErr("Called function was given " + to_string(typedArgs.size()) +
                        " argument(s) but was declared to take 1", loc);

            res = (*fn)(typedArgs[0]);
            gv = GenericValue(res);
        }else if(baseName == "Ante_sizeof"){
            if(typedArgs.size() != 1)
                return c->compErr("Called function was given " + to_string(typedArgs.size()) +
                        " argument(s) but was declared to take 1", loc);

            res = (*fn)(c, typedArgs[0]);
            gv.IntVal = APInt(32, (int)(size_t)res, false);
        }else{
            res = (*fn)();
            gv = GenericValue(res);
        }

        return genericValueToTypedValue(c, gv, fn->retty.get());
    }else{
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();

        auto mod_compiler = wrapFnInModule(c, baseName, mangledName);
        mod_compiler->ast.release();
        auto *mod = mod_compiler->module.release();
        
        if(!mod_compiler or mod_compiler->errFlag or !mod) return 0;

        auto* eBuilder = new EngineBuilder(unique_ptr<llvm::Module>(mod));
        string err;

        //set use interpreter; for some reason both MCJIT and its ORC replacement corrupt/free the memory
        //of c->varTable in some way in four instances: two in the call to jit->finalizeObject() and two
        //in the destructor of jit
        LLVMLinkInInterpreter();
        auto *jit = eBuilder->setErrorStr(&err).setEngineKind(EngineKind::Interpreter).create();

        if(err.length() > 0){
            cerr << err << endl;
            return 0;
        }

        auto args = typedValuesToGenericValues(c, typedArgs, loc, baseName);

        auto *fn = jit->FindFunctionNamed(mangledName.c_str());
        auto genret = jit->runFunction(fn, args);


        //get the type of the function to properly translate the return value
        auto *fnTy = mod_compiler->getFuncDecl(baseName, mangledName)->tv->type.get();
        return genericValueToTypedValue(c, genret, fnTy->extTy.get());
    }
}


bool isInvalidParamType(Type *t){
    return t->isArrayTy();
}

//Computes the address of operator &
TypedValue* addrOf(Compiler *c, TypedValue* tv){
    auto *ptrTy = mkTypeNodeWithExt(TT_Ptr, copy(tv->type));

    if(LoadInst* li = dyn_cast<LoadInst>(tv->val)){
        return new TypedValue(li->getPointerOperand(), ptrTy);
    }else{
        //if it is not stack-allocated already, allocate it on the stack
        auto *alloca = c->builder.CreateAlloca(tv->getType());
        c->builder.CreateStore(tv->val, alloca);
        return new TypedValue(alloca, ptrTy);
    }
}


TypedValue* tryImplicitCast(Compiler *c, TypedValue *arg, TypeNode *castTy){
    if(isNumericTypeTag(arg->type->type) and isNumericTypeTag(castTy->type)){
        auto *widen = c->implicitlyWidenNum(arg, castTy->type);
        if(widen != arg){
            return widen;
        }
    }

    //check for an implicit Cast function
    TypedValue *fn;

    if((fn = c->getCastFn(arg->type.get(), castTy)) and
            !!c->typeEq(arg->type.get(), (const TypeNode*)fn->type->extTy->next.get())){

        //optimize case of Str -> c8* implicit cast
        if(arg->type->typeName == "Str" && fn->val->getName() == "c8*_init_Str"){
            Value *str = arg->val;
            if(str->getType()->isPointerTy())
                str = c->builder.CreateLoad(str);

            return new TypedValue(c->builder.CreateExtractValue(str, 0),
                      mkTypeNodeWithExt(TT_Ptr, mkAnonTypeNode(TT_C8)));
        }else{
            return new TypedValue(c->builder.CreateCall(fn->val, arg->val), fn->type->extTy);
        }
    }else{
        return nullptr;
    }
}


TypedValue* compFnCall(Compiler *c, Node *l, Node *r){
    //used to type-check each parameter later
    vector<TypedValue*> typedArgs;
    vector<Value*> args;

    //add all remaining arguments
    if(auto *tup = dynamic_cast<TupleNode*>(r)){
        auto flag = c->errFlag;
        typedArgs = tup->unpack(c);
        if(c->errFlag != flag) return 0;

        for(TypedValue *v : typedArgs){
            auto *arg = v;
            if(isInvalidParamType(arg->getType()))
                arg = addrOf(c, arg);

            args.push_back(arg->val);
        }
    }else{ //single parameter being applied
        auto *param = r->compile(c);
        if(!param) return 0;

        if(param->type->type != TT_Void){
            auto *arg = param;
            if(isInvalidParamType(arg->getType()))
                arg = addrOf(c, arg);

            typedArgs.push_back(arg);
            args.push_back(arg->val);
        }
    }


    //try to compile the function now that the parameters are compiled.
    TypedValue *tvf = 0;

    //First, check if the lval is a symple VarNode (identifier) and then attempt to
    //inference a method call for it (inference as in if the <type>. syntax is omitted)
    if(VarNode *vn = dynamic_cast<VarNode*>(l)){
        //try to see if arg 1's type contains a method of the same name
        auto params = toTypeNodeVector(typedArgs);

        //try to do module inference
        if(!typedArgs.empty()){
            string fnName = typeNodeToStr(typedArgs[0]->type.get()) + "_" + vn->name;
            tvf = c->getMangledFunction(fnName, params);
        }

        //if the above fails, do regular name mangling only
        if(!tvf) tvf = c->getMangledFunction(vn->name, params);
    }

    //if it is not a varnode/no method is found, then compile it normally
    if(!tvf) tvf = l->compile(c);

    //if there was an error, return
    if(!tvf) return 0;

    //make sure the l val compiles to a function
    if(tvf->type->type != TT_Function && tvf->type->type != TT_Method && tvf->type->type != TT_MetaFunction)
        return c->compErr("Called value is not a function or method, it is a(n) " + 
                typeNodeToColoredStr(tvf->type), l->loc);

    //now that we assured it is a function, unwrap it
    Function *f = (Function*)tvf->val;

    //if tvf is a method, add its host object as the first argument
    if(tvf->type->type == TT_Method){
        TypedValue *obj = ((MethodVal*) tvf)->obj;
        push_front(&args, obj->val);
        push_front(&typedArgs, obj);
    }

    size_t argc = getTupleSize(tvf->type->extTy.get()) - 1;
    if(argc != args.size() and (!f or !f->isVarArg())){
        //check if an empty tuple (a void value) is being applied to a zero argument function before continuing
        //if not checked, it will count it as an argument instead of the absence of any
        //NOTE: this has the possibly unwanted side effect of allowing 't->void function applications to be used
        //      as parameters for functions requiring 0 parameters, although this does not affect the behaviour of either.
        if(argc != 0 || typedArgs[0]->type->type != TT_Void){
            if(args.size() == 1)
                return c->compErr("Called function was given 1 argument but was declared to take " 
                        + to_string(argc), r->loc);
            else
                return c->compErr("Called function was given " + to_string(args.size()) + 
                        " arguments but was declared to take " + to_string(argc), r->loc);
        }
    }

    /* unpack the tuple of arguments into a vector containing each value */
    int i = 1;
    //bool isTemplateFn = false;
    TypeNode *paramTy = (TypeNode*)tvf->type->extTy->next.get();

    //type check each parameter
    for(auto tArg : typedArgs){
        if(!paramTy) break;

        //Mutable parameters are implicitely passed by reference
        //
        //Note that by getting the address of tArg (and not args[i-1])
        //any previous implicit references (like from the passing of an array type)
        //are not applied so no implicit references to references accidentally occur
        if(paramTy->hasModifier(Tok_Mut)){
            args[i-1] = addrOf(c, tArg)->val;
        }

        auto typecheck = c->typeEq(tArg->type.get(), paramTy);
        if(!typecheck){
            TypedValue *cast = tryImplicitCast(c, tArg, paramTy);

            if(cast){
                args[i-1] = cast->val;
                typedArgs[i-1] = cast;
            }else{
                TupleNode *tn = dynamic_cast<TupleNode*>(r);
                if(!tn) return 0;

                size_t index = i - (tvf->type->type == TT_Method ? 2 : 1);
                Node* locNode = tn->exprs[index].get();
                if(!locNode) return 0;

                return c->compErr("Argument " + to_string(i) + " of function is a(n) " + typeNodeToColoredStr(tArg->type)
                    + " but was declared to be a(n) " + typeNodeToColoredStr(paramTy) + " and there is no known implicit cast", locNode->loc);
            }

		//If the types passed type check but still dont match exactly there was probably a void* involved
		//In that case, create a bit cast to the ptr type of the parameter
        }else if(tvf->val and args[i-1]->getType() != tvf->getType()->getPointerElementType()->getFunctionParamType(i-1) and paramTy->type == TT_Ptr){
			args[i-1] = c->builder.CreateBitCast(args[i-1], tvf->getType()->getPointerElementType()->getFunctionParamType(i-1));
		}

        paramTy = (TypeNode*)paramTy->next.get();
        i++;
    }
   
    //if tvf is a ![macro] or similar MetaFunction, then compile it in a separate
    //module and JIT it instead of creating a call instruction
    if(tvf->type->type == TT_MetaFunction){
        string baseName = getName(l);
        string mangledName = mangle(baseName, (TypeNode*)tvf->type->extTy->next.get());
        return compMetaFunctionResult(c, l->loc, baseName, mangledName, typedArgs);
    }

    //use tvf->val as arg, NOT f, (if tvf->val is a function-type parameter then f cannot be called)
    //
    //both a C-style cast and dyn-cast to functions fail if f is a function-pointer
    auto *call = c->builder.CreateCall(tvf->val, args);

    auto *ret = new TypedValue(call, tvf->type->extTy);
    return ret;
}

TypedValue* Compiler::compLogicalOr(Node *lexpr, Node *rexpr, BinOpNode *op){
    Function *f = builder.GetInsertBlock()->getParent();
    auto &blocks = f->getBasicBlockList();

    auto *lhs = lexpr->compile(this);

    auto *curbbl = builder.GetInsertBlock();
    auto *orbb = BasicBlock::Create(*ctxt, "or");
    auto *mergebb = BasicBlock::Create(*ctxt, "merge");

    builder.CreateCondBr(lhs->val, mergebb, orbb);
    blocks.push_back(orbb);
    blocks.push_back(mergebb);


    builder.SetInsertPoint(orbb);
    auto *rhs = rexpr->compile(this);
    
    //the block must be re-gotten in case the expression contains if-exprs, while nodes,
    //or other exprs that change the current block
    auto *curbbr = builder.GetInsertBlock();
    builder.CreateBr(mergebb);
    
    if(rhs->type->type != TT_Bool)
        return compErr("The 'or' operator's rval must be of type bool, but instead is of type "+typeNodeToColoredStr(rhs->type), op->rval->loc);

    builder.SetInsertPoint(mergebb);
    auto *phi = builder.CreatePHI(rhs->getType(), 2);
   
    //short circuit, returning true if return from the first label
    phi->addIncoming(ConstantInt::get(*ctxt, APInt(1, true, true)), curbbl);
    phi->addIncoming(rhs->val, curbbr);

    return new TypedValue(phi, rhs->type);
    
}

TypedValue* Compiler::compLogicalAnd(Node *lexpr, Node *rexpr, BinOpNode *op){
    Function *f = builder.GetInsertBlock()->getParent();
    auto &blocks = f->getBasicBlockList();

    auto *lhs = lexpr->compile(this);

    auto *curbbl = builder.GetInsertBlock();
    auto *andbb = BasicBlock::Create(*ctxt, "and");
    auto *mergebb = BasicBlock::Create(*ctxt, "merge");

    builder.CreateCondBr(lhs->val, andbb, mergebb);
    blocks.push_back(andbb);
    blocks.push_back(mergebb);


    builder.SetInsertPoint(andbb);
    auto *rhs = rexpr->compile(this);

    //the block must be re-gotten in case the expression contains if-exprs, while nodes,
    //or other exprs that change the current block
    auto *curbbr = builder.GetInsertBlock();
    builder.CreateBr(mergebb);

    if(rhs->type->type != TT_Bool)
        return compErr("The 'and' operator's rval must be of type bool, but instead is of type "+typeNodeToColoredStr(rhs->type), op->rval->loc);

    builder.SetInsertPoint(mergebb);
    auto *phi = builder.CreatePHI(rhs->getType(), 2);
   
    //short circuit, returning false if return from the first label
    phi->addIncoming(ConstantInt::get(*ctxt, APInt(1, false, true)), curbbl);
    phi->addIncoming(rhs->val, curbbr);

    return new TypedValue(phi, rhs->type);
}


TypedValue* Compiler::opImplementedForTypes(int op, TypeNode *l, TypeNode *r){
    if(isNumericTypeTag(l->type) && isNumericTypeTag(r->type)){
        switch(op){
            case '+': case '-': case '*': case '/': case '%': return (TypedValue*)1;
        }
    }

    string ls = typeNodeToStr(l);
    string rs = typeNodeToStr(r);
    string baseName = Lexer::getTokStr(op);
    string fullName = baseName + "_" + ls + "_" + rs;
    
    return getFunction(baseName, fullName);
}

TypedValue* handlePrimitiveNumericOp(BinOpNode *bop, Compiler *c, TypedValue *lhs, TypedValue *rhs){
    switch(bop->op){
        case '+': return c->compAdd(lhs, rhs, bop);
        case '-': return c->compSub(lhs, rhs, bop);
        case '*': return c->compMul(lhs, rhs, bop);
        case '/': return c->compDiv(lhs, rhs, bop);
        case '%': return c->compRem(lhs, rhs, bop);
        case '<':
                    if(isFPTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateFCmpOLT(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else if(isUnsignedTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateICmpULT(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else
                        return new TypedValue(c->builder.CreateICmpSLT(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
        case '>':
                    if(isFPTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateFCmpOGT(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else if(isUnsignedTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateICmpUGT(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else
                        return new TypedValue(c->builder.CreateICmpSGT(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
        case '^': return new TypedValue(c->builder.CreateXor(lhs->val, rhs->val), lhs->type);
        case Tok_Eq:
                    if(isFPTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateFCmpOEQ(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else
                        return new TypedValue(c->builder.CreateICmpEQ(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
        case Tok_NotEq:
                    if(isFPTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateFCmpONE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else
                        return new TypedValue(c->builder.CreateICmpNE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
        case Tok_LesrEq:
                    if(isFPTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateFCmpOLE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else if(isUnsignedTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateICmpULE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else
                        return new TypedValue(c->builder.CreateICmpSLE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
        case Tok_GrtrEq:
                    if(isFPTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateFCmpOGE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else if(isUnsignedTypeTag(lhs->type->type))
                        return new TypedValue(c->builder.CreateICmpUGE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
                    else
                        return new TypedValue(c->builder.CreateICmpSGE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
        default:
            return c->compErr("Operator " + Lexer::getTokStr(bop->op) + " is not overloaded for types "
                   + typeNodeToColoredStr(lhs->type) + " and " + typeNodeToColoredStr(rhs->type), bop->loc);
    }
}

/*
 *  Checks the type of a value (usually a function argument) against a type
 *  and attempts to look for and use an implicit conversion if one is found.
 */
TypedValue* typeCheckWithImplicitCasts(Compiler *c, TypedValue *arg, TypeNode *ty){
    auto tc = c->typeEq(arg->type.get(), ty);
    if(!!tc) return arg;

    return tryImplicitCast(c, arg, ty);
}


TypedValue* checkForOperatorOverload(Compiler *c, TypedValue *lhs, int op, TypedValue *rhs){
    string basefn = Lexer::getTokStr(op);
    string mangledfn = mangle(basefn, lhs->type.get(), rhs->type.get());

    //now look for the function
    auto *fn = c->getMangledFunction(basefn, {lhs->type.get(), rhs->type.get()});
    if(!fn) return 0;

    TypeNode *param1 = (TypeNode*)fn->type->extTy->next.get();
    TypeNode *param2 = (TypeNode*)param1->next.get();

    lhs = typeCheckWithImplicitCasts(c, lhs, param1);
    rhs = typeCheckWithImplicitCasts(c, rhs, param2);

    vector<Value*> argVals = {lhs->val, rhs->val};
    return new TypedValue(c->builder.CreateCall(fn->val, argVals), fn->type->extTy);
}


TypedValue* compSequence(Compiler *c, BinOpNode *seq){
    try{
        seq->lval->compile(c);
    }catch(CtError *e){
        delete e;
    }

    //let CompilationError's of rval percolate
    return seq->rval->compile(c);
}


/*
 *  Compiles an operation along with its lhs and rhs
 */
TypedValue* BinOpNode::compile(Compiler *c){
    switch(op){
        case '.': return c->compMemberAccess(lval.get(), (VarNode*)rval.get(), this);
        case '(': return compFnCall(c, lval.get(), rval.get());
        case Tok_And: return c->compLogicalAnd(lval.get(), rval.get(), this);
        case Tok_Or: return c->compLogicalOr(lval.get(), rval.get(), this);
    }
    
    if(op == ';') return compSequence(c, this);

    TypedValue *lhs = lval->compile(c);
    TypedValue *rhs = rval->compile(c);

    if(TypedValue *res = checkForOperatorOverload(c, lhs, op, rhs)){
        return res;
    }

    if(op == '#') return c->compExtract(lhs, rhs, this);


    //Check if both Values are numeric, and if so, check if their types match.
    //If not, do an implicit conversion (usually a widening) to match them.
    c->handleImplicitConversion(&lhs, &rhs);
            

    //first, if both operands are primitive numeric types, use the default ops
    if(isNumericTypeTag(lhs->type->type) && isNumericTypeTag(rhs->type->type)){
        return handlePrimitiveNumericOp(this, c, lhs, rhs);

    //and bools/ptrs are only compatible with == and !=
    }else if((lhs->type->type == TT_Bool and rhs->type->type == TT_Bool) or
             (lhs->type->type == TT_Ptr  and rhs->type->type == TT_Ptr)){
        
        switch(op){
            case Tok_Eq: return new TypedValue(c->builder.CreateICmpEQ(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
            case Tok_NotEq: return new TypedValue(c->builder.CreateICmpNE(lhs->val, rhs->val), mkAnonTypeNode(TT_Bool));
        }
    }

    return c->compErr("Operator " + Lexer::getTokStr(op) + " is not overloaded for types "
            + typeNodeToColoredStr(lhs->type) + " and " + typeNodeToColoredStr(rhs->type), loc);
}


TypedValue* UnOpNode::compile(Compiler *c){
    TypedValue *rhs = rval->compile(c);

    switch(op){
        case '@': //pointer dereference
            if(rhs->type->type != TT_Ptr){
                return c->compErr("Cannot dereference non-pointer type " + typeNodeToColoredStr(rhs->type), loc);
            }
           
            return new TypedValue(c->builder.CreateLoad(rhs->val), rhs->type->extTy);
        case '&': //address-of
            return addrOf(c, rhs);
        case '-': //negation
            return new TypedValue(c->builder.CreateNeg(rhs->val), rhs->type);
        case Tok_Not:
            if(rhs->type->type != TT_Bool)
                return c->compErr("Unary not operator not overloaded for type " + typeNodeToColoredStr(rhs->type), loc);

            return new TypedValue(c->builder.CreateNot(rhs->val), rhs->type);
        case Tok_New:
            //the 'new' keyword in ante creates a reference to any existing value

            if(rhs->getType()->isSized()){
                string mallocFnName = "malloc";
                Function* mallocFn = (Function*)c->getFunction(mallocFnName, mallocFnName)->val;

                unsigned size = rhs->type->getSizeInBits(c) / 8;

                Value *sizeVal = ConstantInt::get(*c->ctxt, APInt(32, size, true));

                Value *voidPtr = c->builder.CreateCall(mallocFn, sizeVal);
                Type *ptrTy = rhs->getType()->getPointerTo();
                Value *typedPtr = c->builder.CreatePointerCast(voidPtr, ptrTy);

                //finally store rhs into the malloc'd slot
                c->builder.CreateStore(rhs->val, typedPtr);

                auto *tyn = mkTypeNodeWithExt(TT_Ptr, copy(rhs->type));
                auto *ret = new TypedValue(typedPtr, tyn);

                //Create an upper-case name so it cannot be referenced normally
                string tmpAllocName = "New_" + typeNodeToStr(rhs->type.get());
                c->stoVar(tmpAllocName, new Variable(tmpAllocName, ret, c->scope, false /*always free*/));

                //return a copy of ret in case it is modified/freed
                return new TypedValue(ret->val, ret->type);
            }
    }
    
    return c->compErr("Unknown unary operator " + Lexer::getTokStr(op), loc);
}
