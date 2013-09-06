/*********************************************************************************/
/** \file parameters.cpp
 *
 * Qt property browser for module parameters
 */
/**********************************************************************************/

#include <QTextEdit>

#include <userinterface/vistleconnection.h>

#include "parameters.h"
#include "vistleobserver.h"

#include <QtGroupPropertyManager>
#include <QtBoolPropertyManager>
#include <QtIntPropertyManager>
#include <QtDoublePropertyManager>
#include <QtStringPropertyManager>
#include <QtEnumPropertyManager>
#include <QtLineEditFactory>
#include <QtSpinBoxFactory>
#include <QtDoubleSpinBoxFactory>

#include "propertybrowser/qtlongpropertymanager.h"
#include "propertybrowser/qtlongeditorfactory.h"

namespace gui {

template<class M>
static M *addPropertyManager(Parameters *p) {
   M *m = new M(p);
   QObject::connect(m, SIGNAL(propertyChanged(QtProperty*)), p, SLOT(propertyChanged(QtProperty*)));
   return m;
}

Parameters::Parameters(QWidget *parent, Qt::WindowFlags f)
   : Parameters::PropertyBrowser(parent)
{
   m_groupManager = new QtGroupPropertyManager(this); // no change notifications

   m_boolManager = addPropertyManager<QtBoolPropertyManager>(this);
   m_intManager = addPropertyManager<QtLongPropertyManager>(this);
   m_intChoiceManager = addPropertyManager<QtEnumPropertyManager>(this);
   m_floatManager = addPropertyManager<QtDoublePropertyManager>(this);
   m_stringManager = addPropertyManager<QtStringPropertyManager>(this);
   m_stringChoiceManager = addPropertyManager<QtEnumPropertyManager>(this);

   QtCheckBoxFactory *checkBoxFactory = new QtCheckBoxFactory(this);
   setFactoryForManager(m_boolManager, checkBoxFactory);

   QtLongSpinBoxFactory *spinBoxFactory = new QtLongSpinBoxFactory(this);
   setFactoryForManager(m_intManager, spinBoxFactory);

   QtDoubleSpinBoxFactory *doubleSpinBoxFactory = new QtDoubleSpinBoxFactory(this);
   setFactoryForManager(m_floatManager, doubleSpinBoxFactory);

   QtLineEditFactory *lineEditFactory = new QtLineEditFactory(this);
   setFactoryForManager(m_stringManager, lineEditFactory);

   QtEnumEditorFactory *comboBoxFactory = new QtEnumEditorFactory(this);
   setFactoryForManager(m_stringChoiceManager, comboBoxFactory);
   setFactoryForManager(m_intChoiceManager, comboBoxFactory);

#if 0
   variantEditor->setFactoryForManager(variantManager, variantFactory);
   variantEditor->addProperty(topItem);
   variantEditor->setPropertiesWithoutValueMarked(true);
   variantEditor->setRootIsDecorated(false);

   variantEditor->show();
#endif

   show();
}

void Parameters::setVistleObserver(VistleObserver *observer)
{
   connect(observer, SIGNAL(newParameter_s(int,QString)),
           this, SLOT(newParameter(int,QString)));
   connect(observer, SIGNAL(parameterValueChanged_s(int,QString)),
           this, SLOT(parameterValueChanged(int,QString)));
   connect(observer, SIGNAL(parameterChoicesChanged_s(int,QString)),
           this, SLOT(parameterChoicesChanged(int,QString)));
}

void Parameters::setVistleConnection(vistle::VistleConnection *conn)
{
   m_vistle = conn;
}

void Parameters::setModule(int id)
{
   clear();
   m_paramToProp.clear();
   m_propToParam.clear();
   m_internalGroup = nullptr;

   m_internalGroup = m_groupManager->addProperty("System Parameters");
   addProperty(m_internalGroup);

   //std::cerr << "Parameters: showing for " << id << std::endl;
   m_moduleId = id;
   auto params = m_vistle->getParameters(id);
   for (auto &p: params)
      newParameter(id, QString::fromStdString(p));
}

void Parameters::newParameter(int moduleId, QString parameterName)
{
   if (m_moduleId != moduleId)
      return;

   parameterValueChanged(moduleId, parameterName);
   parameterChoicesChanged(moduleId, parameterName);
}

void Parameters::parameterValueChanged(int moduleId, QString parameterName)
{
   if (m_moduleId != moduleId)
      return;

   vistle::Parameter *p = m_vistle->getParameter(moduleId, parameterName.toStdString());
   if (!p)
      return;

   QtProperty *prop = nullptr;
   bool insert = false;
   const auto &it = m_paramToProp.find(parameterName);
   if (it != m_paramToProp.end()) {
      prop = it->second;
   } else {
      insert = true;
   }

   //std::cerr << "property " << parameterName.toStdString() << (insert?" to insert":"") << std::endl;

   if (vistle::IntParameter *ip = dynamic_cast<vistle::IntParameter *>(p)) {
      if (ip->presentation() == vistle::Parameter::Boolean) {
         if (!prop)
            prop = m_boolManager->addProperty(parameterName);
         m_boolManager->setValue(prop, ip->getValue() != 0);
      } else if (ip->presentation() == vistle::Parameter::Choice) {
         if (!prop)
            prop = m_intChoiceManager->addProperty(parameterName);
         m_intChoiceManager->setValue(prop, ip->getValue());
#if 0
         QStringList choices;
         for (auto &choice: ip->choices()) {
            choices << QString::fromStdString(choice);
         }
         m_intChoiceManager->setEnumNames(prop, choices);
#endif
      } else {
         if (!prop) {
            prop = m_intManager->addProperty(parameterName);
         }
         m_intManager->setValue(prop, ip->getValue());
      }
   } else if (vistle::FloatParameter *fp = dynamic_cast<vistle::FloatParameter *>(p)) {
      if (!prop) {
         prop = m_floatManager->addProperty(parameterName);
      }
      m_floatManager->setValue(prop, fp->getValue());
   } else if (vistle::StringParameter *sp = dynamic_cast<vistle::StringParameter *>(p)) {
      if (sp->presentation() == vistle::Parameter::Choice) {
         if (!prop) {
            prop = m_stringChoiceManager->addProperty(parameterName);
         }
         int choice = 0;
         m_stringChoiceManager->setValue(prop, choice);
      } else {
         if (!prop) {
            prop = m_stringManager->addProperty(parameterName);
         }
         m_stringManager->setValue(prop, QString::fromStdString(sp->getValue()));
      }
   } else if (vistle::VectorParameter *vp = dynamic_cast<vistle::VectorParameter *>(p)) {
      vistle::ParamVector value = vp->getValue();
      if (!prop) {
         prop = m_groupManager->addProperty(parameterName);
         for (int i=0; i<value.dim; ++i) {
            QString n = QString::number(i);
            if (value.dim < 4) {
               switch(i) {
               case 0: n="x"; break;
               case 1: n="y"; break;
               case 2: n="z"; break;
               case 3: n="w"; break;
               }
            }
            QtProperty *sub = m_floatManager->addProperty(n);
            prop->addSubProperty(sub);
         }
      }
      QList<QtProperty *> subs = prop->subProperties();
      for (int i=0; i<value.dim; ++i) {
         m_floatManager->setValue(subs[i], value[i]);
      }
   } else {
   }

   if (insert && prop) {
      prop->setToolTip(QString::fromStdString(p->description()));
      prop->setWhatsThis(QString::fromStdString(p->description()));
      m_paramToProp[parameterName] = prop;
      m_propToParam[prop] = parameterName;
      if (parameterName.startsWith("_"))
         m_internalGroup->addSubProperty(prop);
      else
         addProperty(prop);
   }
}

void Parameters::parameterChoicesChanged(int moduleId, QString parameterName)
{
   std::cerr << "choices changed" << std::endl;
   if (m_moduleId != moduleId)
      return;

   vistle::Parameter *p = m_vistle->getParameter(moduleId, parameterName.toStdString());
   if (!p)
      return;

   if (p->presentation() != vistle::Parameter::Choice)
      return;

   const auto &it = m_paramToProp.find(parameterName);
   if (it == m_paramToProp.end()) {
      return;
   }

   QtProperty *prop = it->second;

   //std::cerr << "property " << parameterName.toStdString() << (insert?" to insert":"") << std::endl;

   QStringList choices;
   for (auto &choice: p->choices()) {
      choices << QString::fromStdString(choice);
   }
   if (dynamic_cast<vistle::IntParameter *>(p)) {
      m_intChoiceManager->setEnumNames(prop, choices);
   } else if (dynamic_cast<vistle::StringParameter *>(p)) {
      m_stringChoiceManager->setEnumNames(prop, choices);
   } else {
   }
}

void Parameters::propertyChanged(QtProperty *prop)
{
   auto it = m_propToParam.find(prop);
   if (it == m_propToParam.end()) {
      std::cerr << "parameter for property not found: " << prop->propertyName().toStdString() << std::endl;
      return;
   }
   std::string paramName = it->second.toStdString();
   vistle::Parameter *p= m_vistle->getParameter(m_moduleId, paramName);
   assert(p);
   if (!p)
      return;

   bool changed = false;
   if (vistle::IntParameter *ip = dynamic_cast<vistle::IntParameter *>(p)) {
      if (ip->presentation() == vistle::Parameter::Boolean) {
         vistle::Integer value = m_boolManager->value(prop) ? 1 : 0;
         if (ip->getValue() != value) {
            changed = true;
            ip->setValue(value);
         }
      } else if (ip->presentation() == vistle::Parameter::Choice) {
         vistle::Integer value = m_intChoiceManager->value(prop);
         std::cerr << "Choice: value=" << value << std::endl;
         if (ip->getValue() != value) {
            changed = true;
            ip->setValue(value);
         }
      } else {
         if (ip->getValue() != m_intManager->value(prop)) {
            changed = true;
            ip->setValue(m_intManager->value(prop));
         }
      }
   } else if (vistle::FloatParameter *fp = dynamic_cast<vistle::FloatParameter *>(p)) {
      if (fp->getValue() != m_floatManager->value(prop)) {
         changed = true;
         fp->setValue(m_floatManager->value(prop));
      }
   } else if (vistle::StringParameter *sp = dynamic_cast<vistle::StringParameter *>(p)) {
      std::string value;
      if (sp->presentation() == vistle::Parameter::Choice) {
         int choice = m_stringChoiceManager->value(prop);
         QStringList choices = m_stringChoiceManager->enumNames(prop);
         value = choices[choice].toStdString();
      } else {
         value = m_stringManager->value(prop).toStdString();
      }
      if (sp->getValue() != value) {
         changed = true;
         sp->setValue(m_stringManager->value(prop).toStdString());
      }
   } else {
      std::cerr << "property type not handled for " << paramName << std::endl;
      return;
   }
   if (changed) {
      std::cerr << "sending new value for " << paramName << std::endl;
      m_vistle->sendParameter(p);
   }
}

} //namespace gui
